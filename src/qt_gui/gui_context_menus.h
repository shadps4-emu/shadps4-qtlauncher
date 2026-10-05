// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>

#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTableWidget>
#include <QTreeWidgetItem>

#include "create_steam_shortcut.h"
#include "ipc/ipc_client.h"

class GuiContextMenus : public QObject {
    Q_OBJECT
signals:
    void RequestGameListRefresh(const QString& serial);

public:
    int RequestGameMenu(const QPoint& pos, QVector<GameInfo>& m_games,
                        std::shared_ptr<CompatibilityInfoClass> m_compat_info,
                        std::shared_ptr<gui_settings> settings,
                        std::shared_ptr<IpcClient> m_ipc_client, QTableWidget* widget, bool isList,
                        std::function<void(QStringList)> launch_func);

    int GetRowIndex(QTreeWidget* treeWidget, QTreeWidgetItem* item);

private:
    SteamShortcut m_steam_shortcut{nullptr};

    void requestShortcut(const GameInfo& selectedInfo, QString emuPath = "");

    bool convertPngToIco(const QString& pngFilePath, const QString& icoFilePath);

#ifdef Q_OS_WIN
    bool createShortcutWin(const QString& linkPath, const QString& targetPath,
                           const QString& iconPath, const QString& exePath, QString emuPath);
#else
    bool createShortcutLinux(const QString& linkPath, const std::string& name,
                             const QString& targetPath, const QString& iconPath, QString emuPath);
#endif
};
