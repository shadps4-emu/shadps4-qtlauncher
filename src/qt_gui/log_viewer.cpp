// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>

#include <QAction>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextDocument>
#include <QUrl>

#include "log_viewer.h"

namespace {
constexpr int retained_lines = 10000;
constexpr qsizetype max_line_bytes = 16 * 1024;
constexpr qsizetype max_pending_characters = 2 * 1024 * 1024;
constexpr qsizetype batch_characters = 256 * 1024;
constexpr int batch_lines = 256;

class LogHighlighter : public QSyntaxHighlighter {
public:
    explicit LogHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}

protected:
    void highlightBlock(const QString& text) override {
        QColor color = Qt::white;
        if (text.contains("<Critical>")) {
            color = Qt::magenta;
        } else if (text.contains("<Error>")) {
            color = Qt::red;
        } else if (text.contains("<Warning>")) {
            color = Qt::yellow;
        } else if (text.contains("<Trace>")) {
            color = Qt::gray;
        } else if (text.contains("<Debug>")) {
            color = Qt::cyan;
        }
        setFormat(0, static_cast<int>(text.size()), color);
    }
};
} // namespace

LogViewer::LogViewer(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setMaximumBlockCount(retained_lines);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QPalette log_palette = palette();
    log_palette.setColor(QPalette::Base, Qt::black);
    log_palette.setColor(QPalette::Text, Qt::white);
    setPalette(log_palette);
    new LogHighlighter(document());

    flush_timer.setSingleShot(true);
    flush_timer.setTimerType(Qt::PreciseTimer);
    flush_timer.setInterval(16);
    connect(&flush_timer, &QTimer::timeout, this, &LogViewer::FlushPending);

    auto_scroll_action = new QAction(tr("Auto-scroll"), this);
    auto_scroll_action->setCheckable(true);
    auto_scroll_action->setChecked(true);
    connect(auto_scroll_action, &QAction::toggled, this, [this](bool enabled) {
        if (enabled) {
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        }
    });
    open_log_action = new QAction(tr("Open Log File"), this);
    connect(open_log_action, &QAction::triggered, this, [this]() {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(log_path))) {
            QMessageBox::warning(this, tr("Open Log File"), tr("Could not open the log file."));
        }
    });
}

void LogViewer::SetLogPath(const QString& path) {
    flush_timer.stop();
    pending_lines.clear();
    partial_line.clear();
    pending_characters = 0;
    truncated_line = false;
    has_output = false;
    log_path = path;
    clear();
}

void LogViewer::AppendOutput(const QByteArray& bytes) {
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const qsizetype newline = bytes.indexOf('\n', offset);
        const qsizetype end = newline < 0 ? bytes.size() : newline;
        const qsizetype count = std::min(end - offset, max_line_bytes - partial_line.size());
        partial_line.append(bytes.constData() + offset, count);
        truncated_line |= count < end - offset;
        if (newline < 0) {
            break;
        }
        QueueLine();
        offset = newline + 1;
    }
    if (!pending_lines.isEmpty() && !flush_timer.isActive()) {
        flush_timer.start();
    }
}

void LogViewer::QueueLine() {
    if (partial_line.endsWith('\r')) {
        partial_line.chop(1);
    }
    static const QRegularExpression ansi_codes(R"(\x1B\[[0-9;]*[mK])");
    QString line = QString::fromUtf8(partial_line).remove(ansi_codes);
    if (truncated_line) {
        line += tr(" [line truncated; see log file]");
    }
    pending_characters += line.size();
    pending_lines.enqueue(std::move(line));
    // Prefer recent output if the emulator produces more than the UI can display.
    while (pending_lines.size() > retained_lines || pending_characters > max_pending_characters) {
        pending_characters -= pending_lines.dequeue().size();
    }
    partial_line.clear();
    truncated_line = false;
}

void LogViewer::FinishOutput() {
    if (!partial_line.isEmpty() || truncated_line) {
        QueueLine();
    }
    if (!pending_lines.isEmpty() && !flush_timer.isActive()) {
        flush_timer.start();
    }
}

void LogViewer::FlushPending() {
    if (pending_lines.isEmpty()) {
        return;
    }
    QScrollBar* scroll = verticalScrollBar();
    const int old_position = scroll->value();
    const bool follow = auto_scroll_action->isChecked() && old_position == scroll->maximum() &&
                        !scroll->isSliderDown();
    QStringList batch;
    qsizetype characters = 0;
    while (!pending_lines.isEmpty() && batch.size() < batch_lines &&
           characters < batch_characters) {
        QString line = pending_lines.dequeue();
        characters += line.size() + 1;
        pending_characters -= line.size();
        batch.append(std::move(line));
    }
    const int removed = std::max(0, document()->blockCount() + static_cast<int>(batch.size()) -
                                        (has_output ? 0 : 1) - retained_lines);
    QString text = batch.join('\n');
    if (has_output && document()->isEmpty()) {
        text.prepend('\n');
    }
    appendPlainText(text);
    has_output = true;
    scroll->setValue(follow ? scroll->maximum() : std::max(0, old_position - removed));
    if (!pending_lines.isEmpty()) {
        flush_timer.start();
    }
}

void LogViewer::contextMenuEvent(QContextMenuEvent* event) {
    const std::unique_ptr<QMenu> menu(createStandardContextMenu());
    menu->addSeparator();
    menu->addAction(auto_scroll_action);
    open_log_action->setEnabled(QFileInfo(log_path).isFile());
    menu->addAction(open_log_action);
    menu->exec(event->globalPos());
    event->accept();
}
