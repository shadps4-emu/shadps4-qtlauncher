// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QUrl>
#include <QWheelEvent>

#include "log_file_viewer.h"

namespace {
constexpr qint64 scan_bytes = 4 * 1024 * 1024;
constexpr qint64 checkpoint_bytes = 256 * 1024;
constexpr int checkpoint_rows = 1024;
constexpr int max_checkpoints = 256 * 1024;
constexpr int max_line_bytes = 16 * 1024;
constexpr qsizetype window_characters = 128 * 1024;
constexpr qsizetype sample_bytes = 256;

class LogTextEdit : public QPlainTextEdit {
public:
    explicit LogTextEdit(QWidget* parent) : QPlainTextEdit(parent) {
        document()->setDocumentMargin(0);
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this]() { QueueAlignment(); });
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
                [this]() { QueueAlignment(); });
    }

    void SetFollowTail(bool enabled) {
        follow_tail = enabled;
        QueueAlignment();
    }

    bool IsAligning() const {
        return aligning;
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        QueueAlignment();
    }

private:
    void QueueAlignment() {
        if (alignment_pending || aligning) {
            return;
        }
        alignment_pending = true;
        QTimer::singleShot(0, this, [this]() {
            alignment_pending = false;
            aligning = true;
            const auto aligned = qScopeGuard([this]() { aligning = false; });
            if (document()->isEmpty()) {
                setViewportMargins(0, 0, 0, 0);
                verticalScrollBar()->setValue(0);
                return;
            }
            const QTextCursor anchor = cursorForPosition(QPoint(0, 0));
            const int anchor_y = cursorRect(anchor).top() + viewportMargins().top();
            const bool at_bottom = verticalScrollBar()->value() == verticalScrollBar()->maximum();
            constexpr int padding = 4;
            setViewportMargins(padding, 0, padding, padding);
            QTextCursor end(document());
            end.movePosition(QTextCursor::End);
            // Materialize wrapped lines before Qt calculates the bottom scroll position.
            cursorRect(end);
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
            // Keep one pixel beyond the final line so Qt counts it as fully visible.
            const int gap = viewport()->height() - cursorRect(end).bottom() - 2;
            const int top = std::clamp(gap, 0, std::max(0, viewport()->height() - 1));
            if (top > 0) {
                setViewportMargins(padding, top, padding, padding);
                verticalScrollBar()->setValue(verticalScrollBar()->maximum());
            }
            if (!follow_tail && !at_bottom) {
                // Alignment must preserve the reader's position when browsing older output.
                cursorRect(anchor);
                const QTextBlock block = anchor.block();
                const QTextLine line =
                    block.layout()->lineForTextPosition(anchor.positionInBlock());
                const int above = qRound((anchor_y - viewportMargins().top()) /
                                         std::max<qreal>(1, line.height()));
                verticalScrollBar()->setValue(block.firstLineNumber() + line.lineNumber() - above);
            }
        });
    }

    bool follow_tail = true;
    bool alignment_pending = false;
    bool aligning = false;
};

QString DecodeLine(const QByteArray& bytes, bool truncated) {
    static const QRegularExpression ansi(R"(\x1B\[[0-9;]*[mK])");
    QString line = QString::fromUtf8(bytes).remove(ansi);
    if (line.endsWith('\r')) {
        line.chop(1);
    }
    // Embedded separators must not create extra document blocks outside the file's row index.
    line.replace(QChar('\r'), QChar(QChar::LineSeparator));
    line.replace(QChar(QChar::ParagraphSeparator), QChar(QChar::LineSeparator));
    if (truncated) {
        line += QObject::tr(" [line truncated; see log file]");
    }
    return line;
}

QColor LineColor(const QString& line) {
    if (line.contains(QLatin1StringView("<Warning>"))) {
        return Qt::yellow;
    }
    if (line.contains(QLatin1StringView("<Error>"))) {
        return Qt::red;
    }
    if (line.contains(QLatin1StringView("<Critical>"))) {
        return Qt::magenta;
    }
    if (line.contains(QLatin1StringView("<Trace>"))) {
        return Qt::gray;
    }
    if (line.contains(QLatin1StringView("<Debug>"))) {
        return Qt::cyan;
    }
    return Qt::white;
}
} // namespace

void LogFileReader::ResetIndex() {
    checkpoints = {{0, 0}};
    indexed_head.clear();
    indexed_tail.clear();
    scanned = 0;
    completed_rows = 0;
    reported_rows = -1;
    checkpoint_row_stride = checkpoint_rows;
    checkpoint_byte_stride = checkpoint_bytes;
    line_start = 0;
    reset_pending = true;
}

void LogFileReader::SetSource(const QFileInfo& source, int source_generation,
                              bool wait_for_change) {
    path = source.filePath();
    generation = source_generation;
    observed_size = source.exists() ? source.size() : -1;
    modified = source.lastModified();
    waiting = wait_for_change && source.exists();
    finished = false;
    range_count = 0;
    ResetIndex();
    Poll();
}

void LogFileReader::RequestRange(int source_generation, quint64 request, qint64 first, int count,
                                 qint64 anchor) {
    if (source_generation != generation) {
        return;
    }
    range_first = first;
    range_anchor = anchor;
    range_count = count;
    range_request = request;
    Poll();
}

void LogFileReader::Finish() {
    finished = true;
    Poll();
}

void LogFileReader::Poll() {
    const auto completed = qScopeGuard([this]() { emit Polled(); });
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
    if (!info.isFile()) {
        return;
    }
    const qint64 size = info.size();
    const QDateTime timestamp = info.lastModified();
    if (waiting) {
        if (size == observed_size && timestamp == modified) {
            return;
        }
        waiting = false;
    }
    QFile file(path);
    bool replaced = size < scanned;
    if (!replaced && scanned > 0 && observed_size >= 0 &&
        (size != observed_size || timestamp != modified)) {
        // A writer can update timestamps without changing bytes; do not rewind the live tail.
        // A truncated log can also grow past the old size between polls. Check its prefix.
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        replaced = file.read(indexed_head.size()) != indexed_head;
        if (!replaced) {
            if (!file.seek(scanned - indexed_tail.size())) {
                return;
            }
            replaced = file.read(indexed_tail.size()) != indexed_tail;
        }
    }
    if (replaced) {
        ResetIndex();
    }
    observed_size = size;
    modified = timestamp;
    const qint64 current_rows =
        completed_rows + (finished && scanned == size && line_start < size ? 1 : 0);
    if (scanned == size && range_count == 0 && !reset_pending && current_rows == reported_rows) {
        return;
    }
    if ((!file.isOpen() && !file.open(QIODevice::ReadOnly)) || !file.seek(scanned)) {
        return;
    }
    const qint64 previous_rows = completed_rows;
    const qint64 previous_scanned = scanned;
    const QByteArray chunk = file.read(std::min(scan_bytes, size - scanned));
    if (!chunk.isEmpty()) {
        if (indexed_head.size() < sample_bytes) {
            indexed_head.append(chunk.left(sample_bytes - indexed_head.size()));
        }
        indexed_tail = chunk.size() >= sample_bytes ? chunk.right(sample_bytes)
                                                    : (indexed_tail + chunk).right(sample_bytes);
    }
    qsizetype offset = 0;
    while ((offset = chunk.indexOf('\n', offset)) >= 0) {
        ++completed_rows;
        line_start = scanned + offset + 1;
        if (completed_rows - checkpoints.last().row >= checkpoint_row_stride ||
            line_start - checkpoints.last().offset >= checkpoint_byte_stride) {
            checkpoints.append({completed_rows, line_start});
            // Coarsen the sparse index instead of growing it indefinitely.
            if (checkpoints.size() >= max_checkpoints) {
                int retained = 1;
                for (int i = 2; i < checkpoints.size(); i += 2) {
                    checkpoints[retained++] = checkpoints[i];
                }
                const Checkpoint last = checkpoints.last();
                checkpoints.resize(retained);
                if (checkpoints.last().row != last.row) {
                    checkpoints.append(last);
                }
                checkpoint_row_stride *= 2;
                checkpoint_byte_stride *= 2;
            }
        }
        ++offset;
    }
    scanned += chunk.size();
    const qint64 count =
        completed_rows + (finished && scanned == size && line_start < size ? 1 : 0);
    if (reset_pending || previous_rows != completed_rows || previous_scanned != scanned ||
        count != reported_rows) {
        emit Indexed(generation, count, size, reset_pending);
        reported_rows = count;
        reset_pending = false;
    }
    if (range_count > 0) {
        ReadRange();
    }
}

void LogFileReader::ReadRange() {
    QFile file(path);
    QStringList lines;
    const qint64 first = range_first;
    qint64 loaded_first = first;
    const int count = std::exchange(range_count, 0);
    if (file.open(QIODevice::ReadOnly)) {
        auto checkpoint =
            std::upper_bound(checkpoints.begin(), checkpoints.end(), first,
                             [](qint64 row, const Checkpoint& point) { return row < point.row; });
        if (checkpoint != checkpoints.begin()) {
            --checkpoint;
        }
        file.seek(checkpoint->offset);
        qint64 row = checkpoint->row;
        QByteArray partial;
        bool truncated = false;
        qsizetype characters = 0;
        bool enough = false;
        while (file.pos() < scanned && row < first + count && !enough &&
               !QThread::currentThread()->isInterruptionRequested()) {
            const QByteArray chunk = file.read(std::min<qint64>(64 * 1024, scanned - file.pos()));
            if (chunk.isEmpty()) {
                break;
            }
            qsizetype offset = 0;
            while (offset < chunk.size() && row < first + count && !enough) {
                const qsizetype newline = chunk.indexOf('\n', offset);
                const qsizetype end = newline < 0 ? chunk.size() : newline;
                if (row >= first) {
                    const qsizetype length =
                        std::min<qsizetype>(end - offset, max_line_bytes - partial.size());
                    partial.append(chunk.constData() + offset, length);
                    truncated |= length < end - offset;
                }
                if (newline < 0) {
                    break;
                }
                if (row >= first) {
                    QString line = DecodeLine(partial, truncated);
                    if (row > range_anchor && characters + line.size() > window_characters) {
                        enough = true;
                    } else {
                        characters += line.size();
                        lines.append(std::move(line));
                        while (characters > window_characters && lines.size() > 1) {
                            characters -= lines.takeFirst().size();
                            ++loaded_first;
                        }
                    }
                }
                partial.clear();
                truncated = false;
                ++row;
                offset = newline + 1;
            }
        }
        if (finished && file.pos() == scanned && row >= first && !enough && row < first + count &&
            !partial.isEmpty()) {
            QString line = DecodeLine(partial, truncated);
            if (characters + line.size() <= window_characters || row == range_anchor) {
                characters += line.size();
                lines.append(std::move(line));
                while (characters > window_characters && lines.size() > 1) {
                    characters -= lines.takeFirst().size();
                    ++loaded_first;
                }
            }
        }
    }
    emit RangeReady(generation, range_request, loaded_first, lines);
}

LogFileViewer::LogFileViewer(QWidget* parent) : QWidget(parent) {
    text = new LogTextEdit(this);
    text->setReadOnly(true);
    text->setUndoRedoEnabled(false);
    text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QPalette colors = text->palette();
    colors.setColor(QPalette::Base, Qt::black);
    colors.setColor(QPalette::Window, Qt::black);
    colors.setColor(QPalette::Text, Qt::white);
    text->setPalette(colors);
    scroll = new QScrollBar(Qt::Vertical, this);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(text);
    layout->addWidget(scroll);
    auto_scroll = new QAction(tr("Auto-scroll"), this);
    auto_scroll->setCheckable(true);
    auto_scroll->setChecked(true);
    connect(auto_scroll, &QAction::toggled, this, &LogFileViewer::SetAutoScroll);
    connect(scroll, &QScrollBar::sliderPressed, this, [this]() { SetAutoScroll(false); });
    connect(scroll, &QScrollBar::sliderReleased, this,
            [this]() { SetAutoScroll(scroll->sliderPosition() == scroll->maximum()); });
    connect(scroll, &QScrollBar::valueChanged, this, [this](int) { ScrollToRow(ScrollRow()); });
    connect(text->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
        if (!applying && !static_cast<LogTextEdit*>(text)->IsAligning() &&
            !auto_scroll->isChecked()) {
            UpdateAnchor();
            RequestWindow();
        }
    });
    connect(text->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int maximum) {
        // Wrapping and resizing can finish layout after the buffered append.
        if (!applying && !static_cast<LogTextEdit*>(text)->IsAligning() &&
            auto_scroll->isChecked()) {
            text->verticalScrollBar()->setValue(maximum);
        }
    });
    text->viewport()->installEventFilter(this);
    text->installEventFilter(this);
    text->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(text, &QWidget::customContextMenuRequested, this, &LogFileViewer::ShowMenu);

    reader = new LogFileReader;
    reader->moveToThread(&thread);
    connect(&thread, &QThread::finished, reader, &QObject::deleteLater);
    connect(reader, &LogFileReader::Indexed, this,
            [this](int source_generation, qint64 count, qint64, bool reset) {
                if (source_generation != generation) {
                    return;
                }
                if (reset) {
                    cached_lines.clear();
                    rows = 0;
                    target_row = 0;
                    range_pending = false;
                    ++navigation_id;
                    if (count == 0) {
                        applying = true;
                        text->clear();
                        applying = false;
                    }
                }
                const bool changed = count != rows;
                rows = count;
                if (auto_scroll->isChecked()) {
                    target_row = std::max<qint64>(0, rows - 1);
                }
                UpdateScrollRange();
                if (changed || reset) {
                    RequestWindow();
                }
            });
    connect(reader, &LogFileReader::RangeReady, this, &LogFileViewer::ApplyRange);
    refresh_timer.setSingleShot(true);
    refresh_timer.setInterval(16);
    connect(&refresh_timer, &QTimer::timeout, this, &LogFileViewer::RefreshSource);
    connect(reader, &LogFileReader::Polled, this, [this]() {
        poll_pending = false;
        if (!path.isEmpty() && !refresh_timer.isActive()) {
            refresh_timer.start();
        }
    });
    thread.start();
}

LogFileViewer::~LogFileViewer() {
    refresh_timer.stop();
    thread.requestInterruption();
    thread.quit();
    thread.wait();
}

void LogFileViewer::SetSource(const QString& source, bool wait_for_change) {
    refresh_timer.stop();
    path = source;
    ++generation;
    ++navigation_id;
    range_pending = false;
    rows = 0;
    target_row = 0;
    cached_first = 0;
    cached_lines.clear();
    wheel_remainder = 0;
    applying = true;
    text->clear();
    applying = false;
    UpdateScrollRange();
    QFileInfo info(source);
    if (wait_for_change) {
        // Snapshot before launch so a fast writer cannot become the old-file baseline.
        info.stat();
    }
    QMetaObject::invokeMethod(reader,
                              [reader = reader, info, generation = generation, wait_for_change]() {
                                  reader->SetSource(info, generation, wait_for_change);
                              });
}

void LogFileViewer::RefreshSource() {
    // Polling and all file access happen on the worker, never during painting.
    if (!poll_pending) {
        poll_pending = true;
        QMetaObject::invokeMethod(reader, &LogFileReader::Poll);
    }
}

void LogFileViewer::FinishSource() {
    QMetaObject::invokeMethod(reader, &LogFileReader::Finish);
}

qint64 LogFileViewer::RowCount() const {
    return rows;
}

int LogFileViewer::WindowRows() const {
    return std::clamp(text->viewport()->height() / std::max(1, text->fontMetrics().height()) * 4,
                      128, 1024);
}

qint64 LogFileViewer::ScrollRow() const {
    if (scroll->maximum() == 0) {
        return 0;
    }
    return static_cast<qint64>(static_cast<long double>(scroll->value()) *
                               std::max<qint64>(0, rows - 1) / scroll->maximum());
}

void LogFileViewer::UpdateScrollRange() {
    const QSignalBlocker blocker(scroll);
    const qint64 maximum = std::max<qint64>(0, rows - 1);
    scroll->setRange(0,
                     static_cast<int>(std::min<qint64>(maximum, std::numeric_limits<int>::max())));
    const qint64 visible =
        std::max(1, text->cursorForPosition(QPoint(0, text->viewport()->height())).blockNumber() -
                        text->cursorForPosition(QPoint(0, 0)).blockNumber() + 1);
    scroll->setPageStep(static_cast<int>(
        std::max<qint64>(1, maximum > 0 ? visible * scroll->maximum() / maximum : 1)));
    scroll->setValue(auto_scroll->isChecked() ? scroll->maximum()
                     : maximum > 0 ? static_cast<int>(static_cast<long double>(target_row) *
                                                      scroll->maximum() / maximum)
                                   : 0);
}

void LogFileViewer::SetAutoScroll(bool enabled) {
    const QSignalBlocker blocker(auto_scroll);
    auto_scroll->setChecked(enabled);
    static_cast<LogTextEdit*>(text)->SetFollowTail(enabled);
    if (!enabled) {
        UpdateAnchor();
    }
    if (enabled) {
        target_row = std::max<qint64>(0, rows - 1);
        ++navigation_id;
        text->verticalScrollBar()->setValue(text->verticalScrollBar()->maximum());
        RequestWindow();
        UpdateScrollRange();
    }
}

void LogFileViewer::ScrollToRow(qint64 row) {
    if (row != rows - 1 && auto_scroll->isChecked()) {
        SetAutoScroll(false);
    }
    target_row = std::clamp<qint64>(row, 0, std::max<qint64>(0, rows - 1));
    ++navigation_id;
    if (!scroll->isSliderDown() && target_row == rows - 1) {
        SetAutoScroll(true);
    }
    if (target_row >= cached_first && target_row < cached_first + cached_lines.size()) {
        applying = true;
        QTextCursor cursor(
            text->document()->findBlockByNumber(static_cast<int>(target_row - cached_first)));
        PositionCursor(cursor, 0);
        if (auto_scroll->isChecked()) {
            text->verticalScrollBar()->setValue(text->verticalScrollBar()->maximum());
        }
        applying = false;
    }
    RequestWindow();
}

void LogFileViewer::UpdateAnchor() {
    target_row = cached_first + text->cursorForPosition(QPoint(0, 0)).blockNumber();
    ++navigation_id;
    UpdateScrollRange();
}

void LogFileViewer::PositionCursor(const QTextCursor& cursor, int viewport_y) {
    // Materialize the target layout before using its visual-line scrollbar coordinate.
    text->cursorRect(cursor);
    const QTextBlock block = cursor.block();
    const QTextLine line = block.layout()->lineForTextPosition(cursor.positionInBlock());
    const int above = qRound(viewport_y / std::max<qreal>(1, line.height()));
    text->verticalScrollBar()->setValue(block.firstLineNumber() + line.lineNumber() - above);
}

void LogFileViewer::RequestWindow() {
    if (!isVisible() || range_pending || rows == 0) {
        return;
    }
    const int count = WindowRows();
    const qint64 first = auto_scroll->isChecked() || target_row == rows - 1
                             ? std::max<qint64>(0, rows - count)
                             : std::max<qint64>(0, target_row - count / 4);
    if (!auto_scroll->isChecked() && target_row >= cached_first + count / 8 &&
        target_row < cached_first + cached_lines.size() - count / 4) {
        return;
    }
    range_pending = true;
    requested_navigation = navigation_id;
    const quint64 request = ++request_id;
    QMetaObject::invokeMethod(reader, [reader = reader, generation = generation, request, first,
                                       count, anchor = target_row]() {
        reader->RequestRange(generation, request, first, count, anchor);
    });
}

void LogFileViewer::ApplyRange(int source_generation, quint64 request, qint64 first,
                               const QStringList& lines) {
    if (source_generation != generation || request != request_id) {
        return;
    }
    range_pending = false;
    if (requested_navigation != navigation_id) {
        RequestWindow();
        return;
    }
    if (lines.isEmpty()) {
        QTimer::singleShot(50, this, &LogFileViewer::RequestWindow);
        return;
    }
    if (lines == cached_lines && first == cached_first) {
        return;
    }
    const QTextCursor anchor = text->cursorForPosition(QPoint(0, 0));
    const int anchor_y = text->cursorRect(anchor).top();
    const qint64 anchor_row = cached_first + anchor.blockNumber();
    const int anchor_column = anchor.positionInBlock();
    const QTextCursor selection = text->textCursor();
    const QTextBlock selection_start = text->document()->findBlock(selection.anchor());
    const QTextBlock selection_end = text->document()->findBlock(selection.position());
    const qint64 start_row = cached_first + selection_start.blockNumber();
    const qint64 end_row = cached_first + selection_end.blockNumber();
    const int start_column = selection.anchor() - selection_start.position();
    const int end_column = selection.position() - selection_end.position();
    applying = true;
    text->setUpdatesEnabled(false);
    QTextCursor cursor(text->document());
    cursor.beginEditBlock();
    const qint64 overlap = cached_first + cached_lines.size() - first;
    bool incremental =
        !cached_lines.isEmpty() && first >= cached_first && overlap > 0 && overlap <= lines.size();
    if (incremental) {
        for (int i = 0; i < overlap; ++i) {
            if (cached_lines[static_cast<int>(first - cached_first) + i] != lines[i]) {
                incremental = false;
                break;
            }
        }
    }
    int appended = 0;
    if (incremental) {
        if (first > cached_first) {
            cursor.setPosition(text->document()
                                   ->findBlockByNumber(static_cast<int>(first - cached_first))
                                   .position());
            cursor.setPosition(0, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
        appended = static_cast<int>(overlap);
    } else {
        cursor.select(QTextCursor::Document);
        cursor.removeSelectedText();
    }
    cursor.movePosition(QTextCursor::End);
    for (int i = appended; i < lines.size(); ++i) {
        if (i > 0) {
            cursor.insertBlock();
        }
        QTextCharFormat format;
        format.setForeground(LineColor(lines[i]));
        cursor.insertText(lines[i], format);
    }
    cursor.endEditBlock();
    cached_first = first;
    cached_lines = lines;
    if (!incremental && start_row >= first && end_row >= first &&
        start_row < first + lines.size() && end_row < first + lines.size()) {
        const auto start = text->document()->findBlockByNumber(static_cast<int>(start_row - first));
        const auto end = text->document()->findBlockByNumber(static_cast<int>(end_row - first));
        QTextCursor restored(text->document());
        restored.setPosition(start.position() + std::min(start_column, start.length() - 1));
        restored.setPosition(end.position() + std::min(end_column, end.length() - 1),
                             QTextCursor::KeepAnchor);
        text->setTextCursor(restored);
    }
    QScrollBar* native_scroll = text->verticalScrollBar();
    if (auto_scroll->isChecked() || target_row == rows - 1) {
        native_scroll->setValue(native_scroll->maximum());
    } else if (incremental) {
        PositionCursor(anchor, anchor_y);
    } else if (target_row >= first && target_row < first + lines.size()) {
        const QTextBlock block =
            text->document()->findBlockByNumber(static_cast<int>(target_row - first));
        QTextCursor target(block);
        if (target_row == anchor_row) {
            target.setPosition(block.position() + std::min(anchor_column, block.length() - 1));
        }
        PositionCursor(target, anchor_y);
    }
    text->setUpdatesEnabled(true);
    applying = false;
    UpdateScrollRange();
    if (auto_scroll->isChecked() && first + lines.size() < rows) {
        RequestWindow();
    }
}

bool LogFileViewer::eventFilter(QObject* object, QEvent* event) {
    if (object == text->viewport() && event->type() == QEvent::Wheel) {
        const auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
            return QWidget::eventFilter(object, event);
        }
        const int pixels = !wheel->pixelDelta().isNull()
                               ? wheel->pixelDelta().y()
                               : wheel->angleDelta().y() * QApplication::wheelScrollLines() *
                                     text->fontMetrics().height() / 120;
        if (pixels == 0) {
            return QWidget::eventFilter(object, event);
        }
        SetAutoScroll(false);
        QScrollBar* native_scroll = text->verticalScrollBar();
        wheel_remainder +=
            static_cast<qreal>(pixels) / std::max(1, text->fontMetrics().lineSpacing());
        const int steps = static_cast<int>(wheel_remainder);
        wheel_remainder -= steps;
        const int next = native_scroll->value() - steps;
        if (pixels < 0 && next >= native_scroll->maximum() &&
            cached_first + cached_lines.size() >= rows) {
            SetAutoScroll(true);
            return true;
        }
        if (next >= native_scroll->minimum() && next <= native_scroll->maximum()) {
            native_scroll->setValue(next);
        } else {
            ScrollToRow(target_row - steps);
            UpdateScrollRange();
        }
        return true;
    }
    if (object == text && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() & Qt::ControlModifier) {
            if (key->key() == Qt::Key_Home || key->key() == Qt::Key_End) {
                SetAutoScroll(key->key() == Qt::Key_End);
                ScrollToRow(key->key() == Qt::Key_End ? std::max<qint64>(0, rows - 1) : 0);
                UpdateScrollRange();
                return true;
            }
        }
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down ||
            key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown) {
            SetAutoScroll(false);
        }
    }
    return QWidget::eventFilter(object, event);
}

void LogFileViewer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateScrollRange();
    RequestWindow();
}

void LogFileViewer::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    RequestWindow();
}

void LogFileViewer::ShowMenu(const QPoint& position) {
    const auto menu = std::unique_ptr<QMenu>(text->createStandardContextMenu());
    menu->addSeparator();
    menu->addAction(auto_scroll);
    QAction* open = menu->addAction(tr("Open Log File"), this, [this]() {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            QMessageBox::warning(this, tr("Open Log File"), tr("Could not open the log file."));
        }
    });
    open->setEnabled(QFileInfo(path).isFile());
    menu->exec(text->viewport()->mapToGlobal(position));
}
