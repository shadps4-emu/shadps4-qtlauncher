// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

class SettingsDialog;

class SettingsTabDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SettingsTabDialog(SettingsDialog* settings_dialog, const QString& tab_object_name,
                               QWidget* parent = nullptr);
    ~SettingsTabDialog() override;

private:
    SettingsDialog* settings_dialog = nullptr;
};
