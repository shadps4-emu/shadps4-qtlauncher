// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QAbstractScrollArea>
#include <QAbstractTableModel>
#include <QColor>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QPixmap>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

class LogFileWorker : public QThread {
    Q_OBJECT
public:
    explicit LogFileWorker(QObject* parent = nullptr);
    ~LogFileWorker() override;

    void SetSource(const QString& path, int generation, bool wait_for_change);
    void FinishSource(int generation);
    void RequestPage(int first_row, int generation);
    void RequestCopy(int first_row, int last_row, int generation);
    void WakeForFileChange();

signals:
    void Indexed(int generation, int row_count, int changed_row);
    void Reset(int generation);
    void PageReady(int generation, int first_row, const QStringList& lines);
    void CopyReady(int generation, const QString& text);

protected:
    void run() override;

private:
    QMutex mutex;
    QWaitCondition wake;
    QString source_path;
    int source_generation = 0;
    bool wait_for_source_change = false;
    bool source_finished = false;
    qint64 source_baseline_size = -1;
    QDateTime source_baseline_modified;
    bool stopping = false;
    QQueue<int> pages;
    int copy_first = -1;
    int copy_last = -1;
    quint64 wake_revision = 0;
};

class LogFileModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit LogFileModel(QObject* parent = nullptr);
    ~LogFileModel() override = default;

    void SetSource(const QString& path, bool wait_for_change);
    void FinishSource();
    void CopyRows(int first_row, int last_row);
    bool EnsureRowsLoaded(int first_row, int last_row) const;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

signals:
    void LineWidthChanged(int width);
    void PageLoaded();
    void Copied(const QString& text);

private:
    void OnIndexed(int generation, int new_count, int changed_row);
    void OnReset(int generation);
    void OnPageReady(int generation, int first_row, const QStringList& lines);
    void RefreshWatch();

    mutable LogFileWorker worker;
    QFileSystemWatcher watcher;
    QString watched_path;
    mutable QHash<int, QStringList> cached_pages;
    mutable QSet<int> pending_pages;
    QQueue<int> page_order;
    int generation = 0;
    int lines = 0;
};

class LogFileViewer : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit LogFileViewer(QWidget* parent = nullptr);
    LogFileModel* GetModel() const;
    void SetSource(const QString& path, bool wait_for_change);
    void FinishSource();
    void ScrollToBottom();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void QueueRefresh();
    void RefreshFrame();
    void UpdateScrollRange();
    int VisibleRows() const;
    int TargetTopRow() const;
    int RowAt(int y) const;
    LogFileModel* log_model = nullptr;
    int line_height = 0;
    int text_width = 0;
    QTimer frame_timer;
    QPixmap frame;
    QStringList visible_lines;
    QList<QColor> visible_colors;
    QList<bool> visible_selection;
    int displayed_top_row = -1;
    int displayed_horizontal_offset = 0;
    int selection_start = -1;
    int selection_end = -1;
    bool auto_scroll_enabled = true;
    bool follow_tail = true;
    bool applying_frame = false;
};
