// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QPlainTextEdit>
#include <QQueue>
#include <QTimer>

class QAction;

class LogViewer : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit LogViewer(QWidget* parent = nullptr);
    void SetLogPath(const QString& path);
    void AppendOutput(const QByteArray& bytes);
    void FinishOutput();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void QueueLine();
    void FlushPending();

    QTimer flush_timer;
    QQueue<QString> pending_lines;
    QByteArray partial_line;
    QString log_path;
    QAction* auto_scroll_action = nullptr;
    QAction* open_log_action = nullptr;
    qsizetype pending_characters = 0;
    bool truncated_line = false;
    bool has_output = false;
};
