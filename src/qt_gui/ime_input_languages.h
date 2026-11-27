// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <utility>
#include <vector>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

inline constexpr std::array<std::pair<int, const char*>, 35> kImeInputLanguages{{
    {1, "English (US)"},
    {18, "English (UK)"},
    {16, "Polish"},
    {3, "Spanish"},
    {4, "German"},
    {20, "Spanish (Latin America)"},
    {6, "Dutch"},
    {14, "Danish"},
    {8, "Russian"},
    {13, "Swedish"},
    {12, "Finnish"},
    {22, "French (Canada)"},
    {2, "French"},
    {5, "Italian"},
    {15, "Norwegian"},
    {7, "Portuguese (Portugal)"},
    {17, "Portuguese (Brazil)"},
    {19, "Turkish"},
    {0, "Japanese"},
    {31, "Japanese (Gojuon)"},
    {9, "Korean"},
    {11, "Chinese (Simplified)"},
    {32, "Chinese (Hong Kong)"},
    {10, "Chinese (Taiwan)"},
    {33, "Chinese (Zhuyin)"},
    {34, "Chinese (Cangjie)"},
    {21, "Arabic"},
    {27, "Thai"},
    {23, "Czech"},
    {25, "Greek"},
    {24, "Hungarian"},
    {29, "Indonesian"},
    {26, "Romanian"},
    {28, "Vietnamese"},
    {30, "Ukrainian"},
}};

inline void PopulateImeInputLanguages(QListWidget* list, const std::vector<int>& selected) {
    const QSignalBlocker blocker(list);
    list->clear();
    for (const auto& [id, name] : kImeInputLanguages) {
        auto* item = new QListWidgetItem(QString::fromUtf8(name), list);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(std::ranges::find(selected, id) != selected.end() ? Qt::Checked
                                                                              : Qt::Unchecked);
    }
    list->sortItems();
}

inline std::vector<int> GetSelectedImeInputLanguages(const QListWidget* list) {
    std::vector<int> result;
    for (int i = 0; i < list->count(); ++i) {
        const auto* item = list->item(i);
        if (item->checkState() == Qt::Checked) {
            result.push_back(item->data(Qt::UserRole).toInt());
        }
    }
    return result;
}

inline QString ImeInputLanguagesSummary(const std::vector<int>& selected) {
    if (selected.empty()) {
        return QObject::tr("English and console language");
    }
    QStringList names;
    for (const auto& [id, name] : kImeInputLanguages) {
        if (std::ranges::find(selected, id) != selected.end()) {
            names.push_back(QString::fromUtf8(name));
        }
    }
    return names.size() <= 2 ? names.join(", ")
                             : QObject::tr("%1 languages selected").arg(names.size());
}

inline bool EditImeInputLanguages(QWidget* parent, std::vector<int>& selected,
                                  const std::vector<int>& defaults, const QString& default_label,
                                  const QString& title = {}) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title.isEmpty() ? QObject::tr("Text Input Languages") : title);
    dialog.resize(420, 480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* automatic = new QCheckBox(default_label, &dialog);
    automatic->setObjectName("imeInputLanguagesAutomatic");
    auto* list = new QListWidget(&dialog);
    list->setObjectName("imeInputLanguagesList");
    PopulateImeInputLanguages(list, selected.empty() ? defaults : selected);
    automatic->setChecked(selected.empty());
    list->setEnabled(!automatic->isChecked());
    QObject::connect(automatic, &QCheckBox::toggled, list,
                     [list](bool checked) { list->setEnabled(!checked); });
    QObject::connect(list, &QListWidget::itemChanged, list, [list](QListWidgetItem* item) {
        if (GetSelectedImeInputLanguages(list).empty()) {
            const QSignalBlocker blocker(list);
            item->setCheckState(Qt::Checked);
        }
    });
    layout->addWidget(automatic);
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (automatic->isChecked() || !GetSelectedImeInputLanguages(list).empty()) {
            dialog.accept();
        }
    });
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    selected = automatic->isChecked() ? std::vector<int>{} : GetSelectedImeInputLanguages(list);
    return true;
}
