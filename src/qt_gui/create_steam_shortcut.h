// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include "game_info.h"
#include "gui_settings.h"

class SteamShortcut : public QObject {
    Q_OBJECT
public:
    explicit SteamShortcut(std::shared_ptr<gui_settings> settings, QObject* parent = nullptr);

    void requestAddToSteam(const GameInfo& selectedInfo, QString emuPath = "");

private:
    static void vdfWriteString(QByteArray& buf, const QByteArray& key, const QByteArray& value);
    static void vdfWriteInt32(QByteArray& buf, const QByteArray& key, quint32 value);
    static QByteArray buildSteamShortcutEntry(int index, const QString& appName,
                                              const QString& exePath, const QString& startDir,
                                              const QString& iconPath,
                                              const QString& launchOptions);
    static QString findSteamPath();
    bool addNonSteamGame(const QString& shortcutsPath, const QString& appName,
                         const QString& exePath, const QString& startDir, const QString& iconPath,
                         const QString& launchOptions);
    static bool isSteamRunning();
    static void shutdownSteam(const QString& steamPath);
    static bool waitForSteamExit(int timeoutMs = 15000);
    static void relaunchSteam(const QString& steamPath);

    std::shared_ptr<gui_settings> m_gui_settings;
};
