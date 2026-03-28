// SPDX-FileCopyrightText: Copyright 2025 RPCS3 Project
// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QPainter>
#include <QStyle>
#include "gui_settings.h"
#include "table_item_delegate.h"

TableItemDelegate::TableItemDelegate(QObject* parent, bool has_icons)
    : QStyledItemDelegate(parent), m_has_icons(has_icons) {}

void TableItemDelegate::initStyleOption(QStyleOptionViewItem* option,
                                        const QModelIndex& index) const {
    // Remove the focus frame around selected items
    option->state &= ~QStyle::State_HasFocus;

    if (m_has_icons && index.column() == 0) {
        // Don't highlight icons
        option->state &= ~QStyle::State_Selected;

        // Center icons
        option->decorationAlignment = Qt::AlignCenter;
        option->decorationPosition = QStyleOptionViewItem::Top;
    }

    QStyledItemDelegate::initStyleOption(option, index);
}

void TableItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    // Handle attribute fields in SFO viewer (value column, long text with commas)
    if (!m_has_icons && index.column() == 1) { // Value column in SFO viewer
        QString text = index.data(Qt::DisplayRole).toString();

        // Check if this looks like an attribute field (comma-separated text)
        if (text.contains(", ")) {
            QStyleOptionViewItem opt = option;
            initStyleOption(&opt, index);

            // Draw the background
            painter->save();
            QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
            style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

            // Set text color based on selection
            QPalette::ColorGroup cg =
                opt.state & QStyle::State_Enabled ? QPalette::Normal : QPalette::Disabled;
            if (cg == QPalette::Normal && !(opt.state & QStyle::State_Active))
                cg = QPalette::Inactive;

            if (opt.state & QStyle::State_Selected) {
                painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
            } else {
                painter->setPen(opt.palette.color(cg, QPalette::Text));
            }

            // Format text with bullet points
            QStringList items = text.split(", ");
            QString formattedText;
            for (const QString& item : items) {
                if (!formattedText.isEmpty())
                    formattedText += "\n";
                formattedText += "- " + item.trimmed();
            }

            // Draw multiline text
            QRect textRect = opt.rect.adjusted(8, 2, -4, -2); // Extra left padding for bullets
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, formattedText);
            painter->restore();
            return;
        }
    }

    if (m_has_icons && index.column() == 0 && option.state & QStyle::State_Selected) {
        // Add background highlight color to icons
        painter->fillRect(option.rect, option.palette.color(QPalette::Highlight));
    }

    QStyledItemDelegate::paint(painter, option, index);
}

QSize TableItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    // Handle attribute fields in SFO viewer
    if (!m_has_icons && index.column() == 1) { // Value column in SFO viewer
        QString text = index.data(Qt::DisplayRole).toString();

        // Check if this looks like an attribute field
        if (text.contains(", ")) {
            QFontMetrics fm(option.font);

            // Format text with bullet points
            QStringList items = text.split(", ");
            int lineHeight = fm.height();
            int totalHeight = lineHeight * items.count() + 8; // Add padding

            // Find the widest line (including bullet point)
            int maxWidth = 0;
            for (const QString& item : items) {
                int lineWidth = fm.horizontalAdvance("- " + item.trimmed());
                maxWidth = qMax(maxWidth, lineWidth);
            }

            // Add padding to width
            int totalWidth = maxWidth + 12; // Extra padding for bullets

            return QSize(totalWidth, totalHeight);
        }
    }

    return QStyledItemDelegate::sizeHint(option, index);
}
