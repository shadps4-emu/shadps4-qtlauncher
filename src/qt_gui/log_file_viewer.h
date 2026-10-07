// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QAction;
class QFileInfo;
class QScrollBar;
class QPlainTextEdit;
class QTextCursor;

class LogFileReader : public QObject {
    Q_OBJECT
public:
    void SetSource(const QFileInfo& source, int generation, bool wait_for_change);
    void RequestRange(int generation, quint64 request, qint64 first, int count, qint64 anchor);
    void Poll();
    void Finish();

signals:
    void Polled();
    void Indexed(int generation, qint64 rows, qint64 bytes, bool reset);
    void RangeReady(int generation, quint64 request, qint64 first, const QStringList& lines);

private:
    struct Checkpoint {
        qint64 row;
        qint64 offset;
    };
    void ResetIndex();
    void ReadRange();
    QString path;
    QVector<Checkpoint> checkpoints;
    QByteArray indexed_head;
    QByteArray indexed_tail;
    QDateTime modified;
    qint64 observed_size = -1;
    qint64 scanned = 0;
    qint64 completed_rows = 0;
    qint64 reported_rows = -1;
    qint64 checkpoint_row_stride = 1024;
    qint64 checkpoint_byte_stride = 256 * 1024;
    qint64 line_start = 0;
    qint64 range_first = 0;
    qint64 range_anchor = 0;
    int range_count = 0;
    int generation = 0;
    quint64 range_request = 0;
    bool waiting = false;
    bool finished = false;
    bool reset_pending = false;
};

class LogFileViewer : public QWidget {
    Q_OBJECT
public:
    explicit LogFileViewer(QWidget* parent = nullptr);
    ~LogFileViewer() override;
    void SetSource(const QString& path, bool wait_for_change);
    void FinishSource();
    void RefreshSource();
    qint64 RowCount() const;

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void RequestWindow();
    void ApplyRange(int source_generation, quint64 request, qint64 first, const QStringList& lines);
    void UpdateScrollRange();
    void ScrollToRow(qint64 row);
    void PositionCursor(const QTextCursor& cursor, int viewport_y);
    void SetAutoScroll(bool enabled);
    void UpdateAnchor();
    void ShowMenu(const QPoint& position);
    qint64 ScrollRow() const;
    int WindowRows() const;

    QPlainTextEdit* text = nullptr;
    QScrollBar* scroll = nullptr;
    QAction* auto_scroll = nullptr;
    QThread thread;
    LogFileReader* reader = nullptr;
    QTimer refresh_timer;
    QString path;
    QStringList cached_lines;
    qint64 cached_first = 0;
    qint64 rows = 0;
    qint64 target_row = 0;
    int generation = 0;
    quint64 request_id = 0;
    quint64 navigation_id = 0;
    quint64 requested_navigation = 0;
    bool range_pending = false;
    bool applying = false;
    bool poll_pending = false;
    qreal wheel_remainder = 0;
};
