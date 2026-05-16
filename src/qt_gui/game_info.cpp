// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QProgressDialog>

#if WIN32
#include "common/bitlocker.h"
#endif

#include "common/path_util.h"
#include "compatibility_info.h"
#include "core/emulator_settings.h"
#include "game_info.h"

#include <QInputDialog>
#include <QMessageBox>

// Maximum depth to search for games in subdirectories
const int max_recursion_depth = 5;

static bool alreadyAskedBitlocker = false;

void ScanDirectoryRecursively(const QString& dir, QStringList& filePaths, int current_depth = 0) {
    // Stop recursion if we've reached the maximum depth
    if (current_depth >= max_recursion_depth) {
        return;
    }

    QDir directory(dir);

#if WIN32
    QFileInfo dirInfo(dir);
    if (!dirInfo.isReadable() && !alreadyAskedBitlocker) {
        QString drive = dir.split(":").first();

        std::wstring wDrive = drive.toStdWString();
        PCWSTR drivePtr = wDrive.c_str();

        FveInit();

        if (FveIsLocked(drivePtr)) {
        promt:
            bool ok;
            QString key =
                QInputDialog::getText(nullptr, QObject::tr("Drive Locked"),
                                      QObject::tr("Drive %1: is locked. Please enter the "
                                                  "BitLocker key to access it:")
                                          .arg(drive),
                                      QLineEdit::Password, QString(), &ok);
            if (!ok) {
                alreadyAskedBitlocker = true;
                return;
            }

            std::wstring wKey = key.toStdWString();

            HRESULT hr = FveUnlock(drivePtr, wKey.c_str());
            if (hr == 0x80310027) {
                QMessageBox::critical(nullptr, QObject::tr("Error"),
                                      QObject::tr("Incorrect recovery key. Please try again."));
                goto promt;
            }
        }

        FveCleanup();
    }
#endif

    QFileInfoList entries = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const auto& entry : entries) {
        if (entry.fileName().endsWith("-UPDATE") || entry.fileName().endsWith("-patch")) {
            continue;
        }

        // Check if this directory contains a PS4 game (has sce_sys/param.sfo)
        if (QFile::exists(entry.filePath() + "/sce_sys/param.sfo")) {
            filePaths.append(entry.absoluteFilePath());
        } else {
            // If not a game directory, recursively scan it with increased depth
            ScanDirectoryRecursively(entry.absoluteFilePath(), filePaths, current_depth + 1);
        }
    }
}

GameInfoClass::GameInfoClass() = default;
GameInfoClass::~GameInfoClass() = default;

void GameInfoClass::GetGameInfo(QWidget* parent) {
    QStringList filePaths;
    for (const auto& installLoc : EmulatorSettings.GetGameInstallDirs()) {
        QString installDir;
        Common::FS::PathToQString(installDir, installLoc);
        ScanDirectoryRecursively(installDir, filePaths, 0);
    }

    m_games = QtConcurrent::mapped(filePaths, [&](const QString& path) {
                  return readGameInfo(Common::FS::PathFromQString(path));
              }).results();

    // used to retrieve values after performing a search
    m_games_backup = m_games;

    // Progress bar, please be patient :)
    QProgressDialog dialog(tr("Loading game list, please wait :3"), tr("Cancel"), 0, 0, parent);
    dialog.setWindowTitle(tr("Loading..."));

    QFutureWatcher<void> futureWatcher;
    GameListUtils game_util;
    bool finished = false;
    futureWatcher.setFuture(
        QtConcurrent::map(m_games, [&](GameInfo& game) { GameListUtils::GetFolderSize(game); }));
    connect(&futureWatcher, &QFutureWatcher<void>::finished, [&]() {
        dialog.reset();
        std::sort(m_games.begin(), m_games.end(), CompareStrings);
    });
    connect(&dialog, &QProgressDialog::canceled, &futureWatcher, &QFutureWatcher<void>::cancel);
    dialog.setRange(0, m_games.size());
    connect(&futureWatcher, &QFutureWatcher<void>::progressValueChanged, &dialog,
            &QProgressDialog::setValue);

    dialog.exec();
}
