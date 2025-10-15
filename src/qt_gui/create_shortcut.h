// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <QLineEdit>

#include "game_info.h"
#include "gui_settings.h"

class ShortcutDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutDialog(std::shared_ptr<gui_settings> settings, QVector<GameInfo>& m_games,
                            int ID, QWidget* parent = nullptr);
    ~ShortcutDialog();

private:
    void setupUI();
    void browseBinary();
    void browsePatch();
    void createShortcut();
    bool convertPngToIco(const QString& pngFilePath, const QString& icoFilePath);

#ifdef Q_OS_WIN
    bool createShortcutWin(const QString& linkPath, const QString& targetPath,
                           const QString& iconPath, const QString& exePath,
                           const QString& patchPath);

#else
    bool createShortcutLinux(const QString& linkPath, const std::string& name,
                             const QString& targetPath, const QString& iconPath,
                             const QString& shadBinary, const QString& patchPath);
#endif

    // ui elements
    QLineEdit* patchLineEdit;
    QLineEdit* binaryLineEdit;

    std::shared_ptr<gui_settings> m_gui_settings;
    QVector<GameInfo> m_games;
    int itemID;
};
