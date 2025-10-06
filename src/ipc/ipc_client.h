// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>

#include <QFileInfo>
#include <QProcess>

#include "common/memory_patcher.h"

class IpcClient : public QObject {
    Q_OBJECT
public:
    explicit IpcClient(QObject* parent = nullptr);
    void startEmulator(const QFileInfo& exe, const QStringList& args,
                       const QString& workDir = QString());
    void startGame();
    void pauseGame();
    void resumeGame();
    void stopEmulator();
    void restartEmulator();
    void toggleFullscreen();
    void sendMemoryPatches(std::string modNameStr, std::string offsetStr, std::string valueStr,
                           std::string targetStr, std::string sizeStr, bool isOffset,
                           bool littleEndian, MemoryPatcher::PatchMask patchMask, int maskOffset);
    std::function<void()> gameClosedFunc;
    std::function<void()> startGameFunc;
    std::function<void()> restartEmulatorFunc;

    enum ParsingState { normal, args_counter, args };
    std::vector<std::string> parsedArgs;
    std::unordered_map<std::string, bool> supportedCapabilities{
        {"memory_patch", false},
        {"emu_control", false},
    };

private:
    void onStderr();
    void onStdout();
    void onProcessClosed();
    void writeLine(const QString& text);

    QProcess* process = nullptr;
    QByteArray buffer;
    bool pendingRestart = false;

    ParsingState parsingState;
    int argsCounter;
};
