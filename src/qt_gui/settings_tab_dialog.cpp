// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "qt_gui/settings_tab_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include "qt_gui/settings_dialog.h"

SettingsTabDialog::SettingsTabDialog(SettingsDialog* dialog, const QString& tab_object_name,
                                     QWidget* parent)
    : QDialog(parent), settings_dialog(dialog) {
    if (settings_dialog) {
        settings_dialog->setParent(this);
        settings_dialog->SetCloseTarget(this);
        settings_dialog->SetSingleTabScope(tab_object_name);
        settings_dialog->hide();
    }

    QWidget* tab_page = nullptr;
    QString tab_title;
    QTabWidget* tab_widget = settings_dialog ? settings_dialog->GetTabWidget() : nullptr;
    if (settings_dialog) {
        tab_title = settings_dialog->windowTitle();
    }
    if (tab_widget) {
        for (int i = 0; i < tab_widget->count(); ++i) {
            QWidget* candidate = tab_widget->widget(i);
            if (candidate && candidate->objectName() == tab_object_name) {
                tab_page = candidate;
                tab_title = tab_widget->tabText(i);
                tab_widget->removeTab(i);
                break;
            }
        }
    }

    auto layout = new QVBoxLayout(this);
    QSize tab_hint;
    if (tab_page) {
        tab_page->setParent(this);
        tab_page->setVisible(true);
        tab_page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        tab_page->adjustSize();
        tab_hint = tab_page->sizeHint();
        layout->addWidget(tab_page);
    } else {
        layout->addWidget(new QLabel(tr("Settings tab not found."), this));
    }

    QDialogButtonBox* button_box = settings_dialog ? settings_dialog->GetButtonBox() : nullptr;
    QTextEdit* description_text = settings_dialog ? settings_dialog->GetDescriptionText() : nullptr;

    if (settings_dialog) {
        if (auto settings_layout = settings_dialog->layout()) {
            settings_layout->removeWidget(button_box);
            settings_layout->removeWidget(description_text);
        }
    }

    QSize button_hint;
    if (button_box) {
        button_box->setParent(this);
        button_box->adjustSize();
        button_hint = button_box->sizeHint();
        layout->addWidget(button_box);
    }

    QSize description_hint;
    if (description_text) {
        description_text->setParent(this);
        description_text->adjustSize();
        description_hint = description_text->sizeHint();
        layout->addWidget(description_text);
    }

    layout->setStretch(0, 1);
    layout->setStretch(1, 0);
    layout->setStretch(2, 0);
    setLayout(layout);
    if (settings_dialog) {
        QSize base_size = settings_dialog->size();
        if (base_size.isEmpty()) {
            base_size = settings_dialog->sizeHint();
        }
        if (!base_size.isEmpty()) {
            setMinimumSize(base_size);
            resize(base_size);
            return;
        }
    }

    if (!tab_hint.isEmpty()) {
        const int min_width =
            std::max(tab_hint.width(), std::max(button_hint.width(), description_hint.width()));
        const int min_height = tab_hint.height() + button_hint.height() + description_hint.height();
        setMinimumSize(QSize(min_width, min_height));
        resize(QSize(min_width, min_height));
    }
    if (!tab_title.isEmpty()) {
        setWindowTitle(tab_title);
    }
    if (settings_dialog) {
        setWindowIcon(settings_dialog->windowIcon());
    }
}

SettingsTabDialog::~SettingsTabDialog() {
    if (settings_dialog) {
        settings_dialog->close();
    }
}
