// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QPainter>
#include <QStyledItemDelegate>

/** This class is used to get rid of somewhat ugly item focus rectangles. You could change the
 * rectangle instead of omiting it if you wanted */
class TableItemDelegate : public QStyledItemDelegate {
public:
    explicit TableItemDelegate(QObject* parent = nullptr, bool has_icons = false);

    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    bool m_has_icons{};
};
