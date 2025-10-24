// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QListWidget>

#include "gui_settings.h"

class ShortcutDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutDialog(std::shared_ptr<gui_settings> settings, QWidget* parent = nullptr);
    ~ShortcutDialog();

signals:
    void shortcutRequested(QString version);

private:
    void setupUI();
    void createShortcut();

    QListWidget* listWidget;
    std::shared_ptr<gui_settings> m_gui_settings;
};
