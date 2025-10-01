// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

#include "gui_settings.h"

namespace Ui {
class VersionDialog;
}

class VersionDialog : public QDialog {
    Q_OBJECT

public:
    explicit VersionDialog(std::shared_ptr<gui_settings> gui_settings, QWidget* parent = nullptr);
    ~VersionDialog();
    void DownloadListVersion();
    void InstallSelectedVersion();

private:
    Ui::VersionDialog* ui;
    std::shared_ptr<gui_settings> m_gui_settings;

    void LoadinstalledList();
    QStringList LoadDownloadCache();
    void SaveDownloadCache(const QStringList& versions);
    void PopulateDownloadTree(const QStringList& versions);
};
