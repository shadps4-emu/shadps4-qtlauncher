// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

#include "common/config.h"
#include "common/path_util.h"
#include "gui_settings.h"

class QLineEdit;

class GameInstallDialog final : public QDialog {
    Q_OBJECT
public:
    GameInstallDialog();
    ~GameInstallDialog();

private slots:
    void BrowseGamesDirectory();
    void BrowseAddonsDirectory();
    void BrowseVersionDirectory();

private:
    QWidget* SetupGamesDirectory();
    QWidget* SetupAddonsDirectory();
    QWidget* SetupDialogActions();
    QWidget* SetupVersionDirectory();
    void Save();
    std::shared_ptr<gui_settings> m_gui_settings;

private:
    QLineEdit* m_gamesDirectory;
    QLineEdit* m_addonsDirectory;
    QLineEdit* m_versionDirectory;
};