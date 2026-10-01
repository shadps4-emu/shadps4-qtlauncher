// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#include <QAbstractSlider>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPaintEvent>
#include <QPainter>
#include <QRegion>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>

#include "log_file_viewer.h"

namespace {
constexpr qint64 scan_bytes = 1024 * 1024;
constexpr qint64 max_visible_line_bytes = 64 * 1024;
constexpr int page_rows = 128;
constexpr int active_poll_ms = 8;
constexpr int idle_poll_ms = 200;
constexpr int max_cached_pages = 8;
constexpr int frame_interval_ms = 16;

int AlignedLineHeight(const QFontMetrics& metrics, qreal dpr) {
    const int height = metrics.height() + 2;
    for (int candidate = height; candidate < height + 4; ++candidate) {
        if (qFuzzyCompare(candidate * dpr, qreal(qRound(candidate * dpr)))) {
            return candidate;
        }
    }
    return height;
}

int CountLines(const std::vector<qint64>& starts, qint64 size, bool include_partial) {
    return static_cast<int>(starts.size() - 1 + (include_partial && size > starts.back() ? 1 : 0));
}

QString DecodeLine(QByteArray bytes, bool continued) {
    if (bytes.endsWith('\n')) {
        bytes.chop(1);
    }
    if (bytes.endsWith('\r')) {
        bytes.chop(1);
    }
    QString text = QString::fromUtf8(bytes);
    if (continued) {
        text += QStringLiteral(" [line continues]");
    }
    return text;
}

QString ReadLine(QFile& file, const std::vector<qint64>& starts, qint64 size, int row) {
    const qint64 begin = starts[row];
    const qint64 end = row + 1 < starts.size() ? starts[row + 1] : size;
    if (!file.seek(begin)) {
        return {};
    }
    return DecodeLine(file.read(std::min(end - begin, max_visible_line_bytes)),
                      end - begin > max_visible_line_bytes);
}
} // namespace

LogFileWorker::LogFileWorker(QObject* parent) : QThread(parent) {
    start();
}

LogFileWorker::~LogFileWorker() {
    {
        QMutexLocker lock(&mutex);
        stopping = true;
        wake.wakeOne();
    }
    wait();
}

void LogFileWorker::SetSource(const QString& path, int generation, bool wait_for_change) {
    const QFileInfo baseline(path);
    QMutexLocker lock(&mutex);
    source_path = path;
    source_generation = generation;
    wait_for_source_change = wait_for_change;
    source_finished = false;
    source_baseline_size = baseline.exists() ? baseline.size() : -1;
    source_baseline_modified = baseline.lastModified();
    pages.clear();
    copy_first = -1;
    copy_last = -1;
    wake.wakeOne();
}

void LogFileWorker::FinishSource(int generation) {
    QMutexLocker lock(&mutex);
    if (generation == source_generation) {
        source_finished = true;
        wake.wakeOne();
    }
}

void LogFileWorker::RequestPage(int first_row, int generation) {
    int dropped = -1;
    {
        QMutexLocker lock(&mutex);
        if (generation == source_generation && !pages.contains(first_row)) {
            if (pages.size() == max_cached_pages) {
                dropped = pages.dequeue();
            }
            pages.enqueue(first_row);
            wake.wakeOne();
        }
    }
    if (dropped >= 0) {
        emit PageReady(generation, dropped, {});
    }
}

void LogFileWorker::RequestCopy(int first_row, int last_row, int generation) {
    QMutexLocker lock(&mutex);
    if (generation == source_generation) {
        copy_first = first_row;
        copy_last = last_row;
        wake.wakeOne();
    }
}

void LogFileWorker::WakeForFileChange() {
    QMutexLocker lock(&mutex);
    ++wake_revision;
    wake.wakeOne();
}

void LogFileWorker::run() {
    QString path;
    int generation = -1;
    QFile file;
    std::vector<qint64> starts{0};
    qint64 indexed_size = 0;
    QDateTime birth_time;
    QDateTime baseline_modified;
    qint64 baseline_size = -1;
    bool waiting_for_change = false;
    bool include_partial = false;

    while (true) {
        int requested_page = -1;
        int requested_copy_first = -1;
        int requested_copy_last = -1;
        bool finished = false;
        quint64 observed_wake_revision = 0;
        {
            QMutexLocker lock(&mutex);
            if (stopping) {
                return;
            }
            if (generation != source_generation) {
                generation = source_generation;
                path = source_path;
                waiting_for_change = wait_for_source_change;
                include_partial = false;
                file.close();
                file.setFileName(path);
                starts.assign(1, 0);
                indexed_size = 0;
                birth_time = {};
                baseline_size = source_baseline_size;
                baseline_modified = source_baseline_modified;
            }
            if (!pages.isEmpty()) {
                requested_page = pages.dequeue();
            }
            requested_copy_first = copy_first;
            requested_copy_last = copy_last;
            finished = source_finished;
            observed_wake_revision = wake_revision;
            copy_first = -1;
            copy_last = -1;
        }

        if (path.isEmpty()) {
            QMutexLocker lock(&mutex);
            if (source_generation == generation && !stopping) {
                wake.wait(&mutex);
            }
            continue;
        }

        const QFileInfo info(path);
        const bool exists = !path.isEmpty() && info.isFile();
        if (waiting_for_change) {
            if (!exists ||
                (info.size() == baseline_size && info.lastModified() == baseline_modified)) {

                QMutexLocker lock(&mutex);
                wake.wait(&mutex, finished ? idle_poll_ms : active_poll_ms);
                continue;
            }
            waiting_for_change = false;
        }

        if (!exists) {
            if (file.isOpen()) {
                file.close();
                starts.assign(1, 0);
                indexed_size = 0;
                birth_time = {};
                emit Reset(generation);
            }
            QMutexLocker lock(&mutex);
            wake.wait(&mutex, finished ? idle_poll_ms : active_poll_ms);
            continue;
        }

        if (file.isOpen() &&
            (info.size() < indexed_size || (birth_time.isValid() && info.birthTime().isValid() &&
                                            info.birthTime() != birth_time))) {

            file.close();
            starts.assign(1, 0);
            indexed_size = 0;
            emit Reset(generation);
        }
        if (!file.isOpen()) {
            if (!file.open(QIODevice::ReadOnly)) {
                QMutexLocker lock(&mutex);
                wake.wait(&mutex, idle_poll_ms);
                continue;
            }
            birth_time = info.birthTime();
        }

        if (finished && !include_partial && indexed_size >= info.size()) {
            include_partial = true;
            if (indexed_size > starts.back()) {
                emit Indexed(generation, CountLines(starts, indexed_size, include_partial), -1);
            }
        }

        if (requested_page >= 0 &&
            requested_page < CountLines(starts, indexed_size, include_partial)) {
            QStringList result;
            const int end = std::min(requested_page + page_rows,
                                     CountLines(starts, indexed_size, include_partial));
            result.reserve(end - requested_page);
            const qint64 begin_byte = starts[requested_page];
            const qint64 end_byte = end < starts.size() ? starts[end] : indexed_size;
            QByteArray page_bytes;
            if (end_byte - begin_byte <= scan_bytes && file.seek(begin_byte)) {
                page_bytes = file.read(end_byte - begin_byte);
            }
            for (int row = requested_page; row < end; ++row) {
                const qint64 line_end = row + 1 < starts.size() ? starts[row + 1] : indexed_size;
                if (page_bytes.size() == end_byte - begin_byte) {
                    const qint64 length = line_end - starts[row];
                    result.append(
                        DecodeLine(page_bytes.sliced(starts[row] - begin_byte,
                                                     std::min(length, max_visible_line_bytes)),
                                   length > max_visible_line_bytes));
                } else {
                    result.append(ReadLine(file, starts, indexed_size, row));
                }
            }
            emit PageReady(generation, requested_page, result);
        }

        if (requested_copy_first >= 0) {
            constexpr qint64 max_copy_bytes = 32 * 1024 * 1024;
            QString text;
            const int end = std::min(requested_copy_last,
                                     CountLines(starts, indexed_size, include_partial) - 1);
            bool limited = end < requested_copy_last;
            if (requested_copy_first <= end) {
                const qint64 begin = starts[requested_copy_first];
                const qint64 end_byte = end + 1 < starts.size() ? starts[end + 1] : indexed_size;
                if (file.seek(begin)) {
                    const QByteArray bytes = file.read(std::min(end_byte - begin, max_copy_bytes));
                    limited |= bytes.size() < end_byte - begin;
                    text = QString::fromUtf8(bytes);
                    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
                    if (text.endsWith('\n')) {
                        text.chop(1);
                    }
                    if (text.endsWith('\r')) {
                        text.chop(1);
                    }
                } else {
                    limited = true;
                }
            }
            if (limited) {
                text += QStringLiteral("\n[Copy limited to 32 MiB]");
            }
            emit CopyReady(generation, text);
        }

        if (indexed_size < info.size()) {
            if (!file.seek(indexed_size)) {
                continue;
            }
            const QByteArray bytes = file.read(std::min(scan_bytes, info.size() - indexed_size));
            if (!bytes.isEmpty()) {
                const int old_count = CountLines(starts, indexed_size, include_partial);
                const int changed_row =
                    include_partial && indexed_size > starts.back() ? old_count - 1 : -1;
                const char* cursor = bytes.constData();
                const char* const end = cursor + bytes.size();
                while (cursor < end && starts.size() < std::numeric_limits<int>::max()) {
                    const char* newline = static_cast<const char*>(
                        std::memchr(cursor, '\n', static_cast<size_t>(end - cursor)));
                    if (!newline) {
                        break;
                    }
                    starts.push_back(indexed_size + (newline - bytes.constData()) + 1);
                    cursor = newline + 1;
                }
                indexed_size += bytes.size();
                const int new_count = CountLines(starts, indexed_size, include_partial);
                if (new_count != old_count || changed_row >= 0) {
                    emit Indexed(generation, new_count, changed_row);
                }
                continue;
            }
        }

        QMutexLocker lock(&mutex);
        if (source_generation == generation && pages.isEmpty() && copy_first < 0 &&
            source_finished == finished && wake_revision == observed_wake_revision && !stopping) {

            wake.wait(&mutex, finished ? idle_poll_ms : active_poll_ms);
        }
    }
}

LogFileModel::LogFileModel(QObject* parent) : QAbstractTableModel(parent) {
    connect(&worker, &LogFileWorker::Indexed, this, &LogFileModel::OnIndexed);
    connect(&worker, &LogFileWorker::Reset, this, &LogFileModel::OnReset);
    connect(&worker, &LogFileWorker::PageReady, this, &LogFileModel::OnPageReady);
    connect(&worker, &LogFileWorker::CopyReady, this, [this](int source, const QString& text) {
        if (source == generation) {
            emit Copied(text);
        }
    });
    const auto on_file_changed = [this]() {
        RefreshWatch();
        worker.WakeForFileChange();
    };
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, on_file_changed);
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, on_file_changed);
}

void LogFileModel::SetSource(const QString& path, bool wait_for_change) {
    ++generation;
    beginResetModel();
    lines = 0;
    cached_pages.clear();
    pending_pages.clear();
    page_order.clear();
    endResetModel();
    worker.SetSource(path, generation, wait_for_change);
    watched_path = path;
    if (!watcher.files().isEmpty()) {
        watcher.removePaths(watcher.files());
    }
    if (!watcher.directories().isEmpty()) {
        watcher.removePaths(watcher.directories());
    }
    RefreshWatch();
}

void LogFileModel::RefreshWatch() {
    if (watched_path.isEmpty()) {
        return;
    }
    const QFileInfo info(watched_path);
    const QString directory = info.absolutePath();
    if (!watcher.directories().contains(directory) && QDir(directory).exists()) {
        watcher.addPath(directory);
    }
    if (!watcher.files().contains(watched_path) && info.isFile()) {
        watcher.addPath(watched_path);
    }
}

void LogFileModel::FinishSource() {
    worker.FinishSource(generation);
}

void LogFileModel::CopyRows(int first_row, int last_row) {
    worker.RequestCopy(first_row, last_row, generation);
}

bool LogFileModel::EnsureRowsLoaded(int first_row, int last_row) const {
    bool ready = true;
    for (int first = first_row / page_rows * page_rows; first <= last_row; first += page_rows) {
        const auto page = cached_pages.constFind(first);
        const int last_offset = std::min(last_row - first, page_rows - 1);
        if (page != cached_pages.cend() && page->size() > last_offset) {
            continue;
        }
        ready = false;
        if (!pending_pages.contains(first)) {
            pending_pages.insert(first);
            worker.RequestPage(first, generation);
        }
    }
    return ready;
}

int LogFileModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : lines;
}

int LogFileModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 1;
}

QVariant LogFileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= lines || index.column() != 0 ||
        (role != Qt::DisplayRole && role != Qt::ForegroundRole)) {

        return {};
    }
    const int first = index.row() / page_rows * page_rows;
    const auto page = cached_pages.constFind(first);
    const int offset = index.row() - first;
    if (page == cached_pages.cend() || offset >= page->size()) {
        if (!pending_pages.contains(first)) {
            pending_pages.insert(first);
            worker.RequestPage(first, generation);
        }
        return {};
    }
    const QString& line = page->at(offset);
    if (role == Qt::DisplayRole) {
        return line;
    }
    if (role == Qt::ForegroundRole) {
        if (line.contains("<Critical>")) {
            return QColor(Qt::magenta);
        }
        if (line.contains("<Error>")) {
            return QColor(Qt::red);
        }
        if (line.contains("<Warning>")) {
            return QColor(Qt::yellow);
        }
        if (line.contains("<Trace>")) {
            return QColor(Qt::gray);
        }
        if (line.contains("<Debug>")) {
            return QColor(Qt::cyan);
        }
        return QColor(Qt::white);
    }
    return {};
}

void LogFileModel::OnIndexed(int source, int new_count, int changed_row) {
    if (source != generation) {
        return;
    }
    if (changed_row >= 0 && changed_row < lines) {
        const int first = changed_row / page_rows * page_rows;
        if (cached_pages.contains(first) && !pending_pages.contains(first)) {
            pending_pages.insert(first);
            worker.RequestPage(first, generation);
        }
    }
    if (new_count > lines) {
        beginInsertRows({}, lines, new_count - 1);
        lines = new_count;
        endInsertRows();
    }
}

void LogFileModel::OnReset(int source) {
    if (source != generation) {
        return;
    }
    beginResetModel();
    lines = 0;
    cached_pages.clear();
    pending_pages.clear();
    page_order.clear();
    endResetModel();
}

void LogFileModel::OnPageReady(int source, int first_row, const QStringList& content) {
    if (source != generation) {
        return;
    }
    pending_pages.remove(first_row);
    if (content.isEmpty() || first_row >= lines) {
        return;
    }
    int unchanged = 0;
    const auto old_page = cached_pages.constFind(first_row);
    if (old_page != cached_pages.cend()) {
        while (unchanged < old_page->size() && unchanged < content.size() &&
               old_page->at(unchanged) == content.at(unchanged)) {
            ++unchanged;
        }
    }
    cached_pages.insert(first_row, content);
    page_order.removeAll(first_row);
    page_order.enqueue(first_row);
    while (page_order.size() > max_cached_pages) {
        cached_pages.remove(page_order.dequeue());
    }
    const QFontMetrics metrics(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    int width = 0;
    for (int offset = unchanged; offset < content.size(); ++offset) {
        width = std::max(width, metrics.horizontalAdvance(content.at(offset)));
    }
    if (width > 0) {
        emit LineWidthChanged(width + 24);
    }
    const int last_row = std::min(first_row + static_cast<int>(content.size()), lines) - 1;
    if (first_row + unchanged <= last_row) {
        emit dataChanged(index(first_row + unchanged, 0), index(last_row, 0));
    }
    emit PageLoaded();
}

LogFileViewer::LogFileViewer(QWidget* parent)
    : QAbstractScrollArea(parent), log_model(new LogFileModel(this)) {
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QPalette log_palette = palette();
    log_palette.setColor(QPalette::Base, Qt::black);
    setPalette(log_palette);
    viewport()->setAutoFillBackground(false);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    frame_timer.setSingleShot(true);
    frame_timer.setTimerType(Qt::PreciseTimer);
    frame_timer.setInterval(frame_interval_ms);
    connect(&frame_timer, &QTimer::timeout, this, &LogFileViewer::RefreshFrame);
    line_height = AlignedLineHeight(fontMetrics(), viewport()->devicePixelRatioF());
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    verticalScrollBar()->setSingleStep(3);
    horizontalScrollBar()->setSingleStep(fontMetrics().horizontalAdvance(QLatin1Char('M')) * 3);

    auto* copy_action = new QAction(tr("Copy"), this);
    copy_action->setShortcut(QKeySequence::Copy);
    addAction(copy_action);
    viewport()->addAction(copy_action);
    viewport()->setContextMenuPolicy(Qt::DefaultContextMenu);
    connect(copy_action, &QAction::triggered, this, [this]() {
        if (selection_start >= 0 && selection_end >= 0) {
            log_model->CopyRows(std::min(selection_start, selection_end),
                                std::max(selection_start, selection_end));
        }
    });
    auto* auto_scroll_action = new QAction(tr("Auto-scroll"), this);
    auto_scroll_action->setCheckable(true);
    auto_scroll_action->setChecked(true);
    addAction(auto_scroll_action);
    viewport()->addAction(auto_scroll_action);
    connect(auto_scroll_action, &QAction::toggled, this, [this](bool enabled) {
        auto_scroll_enabled = enabled;
        if (enabled) {
            follow_tail = true;
        }
        QueueRefresh();
    });
    connect(log_model, &LogFileModel::Copied, this,
            [](const QString& text) { QApplication::clipboard()->setText(text); });

    connect(log_model, &LogFileModel::LineWidthChanged, this, [this](int width) {
        if (width > text_width) {
            text_width = width;
            UpdateScrollRange();
        }
    });
    connect(log_model, &QAbstractItemModel::modelReset, this, [this]() {
        selection_start = selection_end = -1;
        text_width = 0;
        frame = {};
        visible_lines.clear();
        visible_colors.clear();
        visible_selection.clear();
        displayed_top_row = -1;
        UpdateScrollRange();
        viewport()->update();
        QueueRefresh();
    });
    connect(log_model, &QAbstractItemModel::rowsInserted, this, [this]() {
        UpdateScrollRange();
        QueueRefresh();
    });
    connect(log_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { QueueRefresh(); });
    connect(log_model, &LogFileModel::PageLoaded, this, &LogFileViewer::QueueRefresh);
    QScrollBar* scroll = verticalScrollBar();
    connect(scroll, &QScrollBar::rangeChanged, this, &LogFileViewer::QueueRefresh);
    connect(scroll, &QScrollBar::valueChanged, this, [this, scroll](int value) {
        if (!applying_frame) {
            follow_tail = !scroll->isSliderDown() && value == scroll->maximum();
            QueueRefresh();
        }
    });
    connect(scroll, &QScrollBar::actionTriggered, this, [this, scroll](int) {
        follow_tail = !scroll->isSliderDown() && scroll->sliderPosition() == scroll->maximum();
        QueueRefresh();
    });
    connect(scroll, &QScrollBar::sliderPressed, this, [this]() {
        follow_tail = false;
        QueueRefresh();
    });
    connect(scroll, &QScrollBar::sliderMoved, this, [this](int) {
        follow_tail = false;
        QueueRefresh();
    });
    connect(scroll, &QScrollBar::sliderReleased, this, [this, scroll]() {
        follow_tail = scroll->sliderPosition() == scroll->maximum();
        QueueRefresh();
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &LogFileViewer::QueueRefresh);
}

LogFileModel* LogFileViewer::GetModel() const {
    return log_model;
}

void LogFileViewer::SetSource(const QString& path, bool wait_for_change) {
    frame_timer.stop();
    follow_tail = true;
    UpdateScrollRange();
    log_model->SetSource(path, wait_for_change);
    QueueRefresh();
}

void LogFileViewer::FinishSource() {
    log_model->FinishSource();
}

void LogFileViewer::ScrollToBottom() {
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void LogFileViewer::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.addActions(actions());
    menu.exec(event->globalPos());
    event->accept();
}

void LogFileViewer::paintEvent(QPaintEvent* event) {
    QPainter painter(viewport());
    if (frame.isNull()) {
        painter.fillRect(event->rect(), palette().color(QPalette::Base));
    } else {
        // The screen only sees a complete frame, never an unloaded cache page.
        painter.drawPixmap(0, 0, frame);
        const QSize size = frame.deviceIndependentSize().toSize();
        if (size.width() < viewport()->width()) {
            painter.fillRect(size.width(), 0, viewport()->width() - size.width(),
                             viewport()->height(), palette().color(QPalette::Base));
        }
        if (size.height() < viewport()->height()) {
            painter.fillRect(0, size.height(), viewport()->width(),
                             viewport()->height() - size.height(), palette().color(QPalette::Base));
        }
    }
}

void LogFileViewer::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    line_height = AlignedLineHeight(fontMetrics(), viewport()->devicePixelRatioF());
    UpdateScrollRange();
    QueueRefresh();
}

void LogFileViewer::showEvent(QShowEvent* event) {
    QAbstractScrollArea::showEvent(event);
    line_height = AlignedLineHeight(fontMetrics(), viewport()->devicePixelRatioF());
    UpdateScrollRange();
    QueueRefresh();
}

void LogFileViewer::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        selection_start = selection_end = RowAt(event->position().y());
        QueueRefresh();
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void LogFileViewer::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton && selection_start >= 0) {
        selection_end = RowAt(event->position().y());
        QueueRefresh();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void LogFileViewer::keyPressEvent(QKeyEvent* event) {
    QScrollBar* scroll = verticalScrollBar();
    if (event->matches(QKeySequence::SelectAll) && log_model->rowCount() > 0) {
        selection_start = 0;
        selection_end = log_model->rowCount() - 1;
        QueueRefresh();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Up:
        scroll->triggerAction(QAbstractSlider::SliderSingleStepSub);
        break;
    case Qt::Key_Down:
        scroll->triggerAction(QAbstractSlider::SliderSingleStepAdd);
        break;
    case Qt::Key_PageUp:
        scroll->triggerAction(QAbstractSlider::SliderPageStepSub);
        break;
    case Qt::Key_PageDown:
        scroll->triggerAction(QAbstractSlider::SliderPageStepAdd);
        break;
    case Qt::Key_Home:
        scroll->triggerAction(QAbstractSlider::SliderToMinimum);
        break;
    case Qt::Key_End:
        scroll->triggerAction(QAbstractSlider::SliderToMaximum);
        break;
    default:
        QAbstractScrollArea::keyPressEvent(event);
    }
}

void LogFileViewer::scrollContentsBy(int, int) {}

void LogFileViewer::UpdateScrollRange() {
    QScrollBar* vertical = verticalScrollBar();
    vertical->setPageStep(VisibleRows());
    vertical->setRange(0, std::max(0, log_model->rowCount() - VisibleRows()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setRange(0, std::max(0, text_width - viewport()->width()));
}

int LogFileViewer::VisibleRows() const {
    return std::max(1, viewport()->height() / line_height);
}

int LogFileViewer::TargetTopRow() const {
    const QScrollBar* scroll = verticalScrollBar();
    if (scroll->isSliderDown()) {
        return scroll->sliderPosition();
    }
    return auto_scroll_enabled && follow_tail ? scroll->maximum() : scroll->value();
}

int LogFileViewer::RowAt(int y) const {
    return log_model->rowCount() == 0 ? -1
                                      : std::clamp(std::max(0, displayed_top_row) + y / line_height,
                                                   0, log_model->rowCount() - 1);
}

void LogFileViewer::QueueRefresh() {
    const int top = TargetTopRow();
    const int last = std::min(log_model->rowCount() - 1,
                              top + (viewport()->height() + line_height - 1) / line_height - 1);
    if (last >= top) {
        log_model->EnsureRowsLoaded(top, last);
    }
    if (!frame_timer.isActive()) {
        frame_timer.start();
    }
}

void LogFileViewer::RefreshFrame() {
    QScrollBar* scroll = verticalScrollBar();
    const int top_row = TargetTopRow();
    const int last_row =
        std::min(log_model->rowCount() - 1,
                 top_row + (viewport()->height() + line_height - 1) / line_height - 1);
    if (last_row >= top_row && !log_model->EnsureRowsLoaded(top_row, last_row)) {
        return;
    }

    QStringList next_lines;
    QList<QColor> next_colors;
    QList<bool> next_selection;
    for (int row = top_row; row <= last_row; ++row) {
        const QModelIndex item = log_model->index(row, 0);
        next_lines.append(log_model->data(item, Qt::DisplayRole).toString());
        next_colors.append(log_model->data(item, Qt::ForegroundRole).value<QColor>());
        next_selection.append(selection_start >= 0 && selection_end >= 0 &&
                              row >= std::min(selection_start, selection_end) &&
                              row <= std::max(selection_start, selection_end));
    }

    const qreal dpr = viewport()->devicePixelRatioF();
    const QSize pixels = (QSizeF(viewport()->size()) * dpr).toSize();
    const int horizontal_offset = horizontalScrollBar()->value();
    const int delta = top_row - displayed_top_row;
    if (!frame.isNull() && frame.size() == pixels && frame.devicePixelRatioF() == dpr &&
        delta == 0 && displayed_horizontal_offset == horizontal_offset &&
        visible_lines == next_lines && visible_colors == next_colors &&
        visible_selection == next_selection) {

        return;
    }
    const bool reuse =
        !frame.isNull() && frame.size() == pixels && frame.devicePixelRatioF() == dpr &&
        displayed_horizontal_offset == horizontal_offset && std::abs(delta) < VisibleRows() &&
        (delta == 0 || qFuzzyCompare(line_height * dpr, qreal(qRound(line_height * dpr))));
    if (!reuse) {
        frame = QPixmap(pixels);
        frame.setDevicePixelRatio(dpr);
        frame.fill(palette().color(QPalette::Base));
    } else if (delta != 0) {
        frame.scroll(0, -qRound(delta * line_height * dpr), frame.rect());
    }

    QPainter painter(&frame);
    painter.setFont(font());
    QRegion changed_region;
    for (int offset = 0; offset < next_lines.size(); ++offset) {
        const int old_offset = offset + delta;
        const bool retained = reuse && old_offset >= 0 && old_offset < visible_lines.size() &&
                              // A formerly clipped bottom row needs its full text drawn.
                              (old_offset + 1) * line_height <= viewport()->height() &&
                              visible_lines.at(old_offset) == next_lines.at(offset) &&
                              visible_colors.at(old_offset) == next_colors.at(offset) &&
                              visible_selection.at(old_offset) == next_selection.at(offset);
        if (retained) {
            continue;
        }
        const int y = offset * line_height;
        changed_region += QRect(0, y, viewport()->width(), line_height);
        painter.fillRect(
            0, y, viewport()->width(), line_height,
            palette().color(next_selection.at(offset) ? QPalette::Highlight : QPalette::Base));
        painter.setPen(next_selection.at(offset) ? palette().color(QPalette::HighlightedText)
                                                 : next_colors.at(offset));
        painter.drawText(4 - horizontal_offset, y + fontMetrics().ascent() + 1,
                         next_lines.at(offset));
    }
    const int bottom = static_cast<int>(next_lines.size()) * line_height;
    if (bottom < viewport()->height()) {
        changed_region += QRect(0, bottom, viewport()->width(), viewport()->height() - bottom);
        painter.fillRect(0, bottom, viewport()->width(), viewport()->height() - bottom,
                         palette().color(QPalette::Base));
    }
    painter.end();
    visible_lines = std::move(next_lines);
    visible_colors = std::move(next_colors);
    visible_selection = std::move(next_selection);
    displayed_top_row = top_row;
    displayed_horizontal_offset = horizontal_offset;
    applying_frame = true;
    if (!scroll->isSliderDown()) {
        scroll->setValue(top_row);
    }
    applying_frame = false;
    if (reuse) {
        if (delta != 0) {
            viewport()->scroll(0, -delta * line_height);
        }
        viewport()->update(changed_region);
    } else {
        viewport()->update();
    }
}
