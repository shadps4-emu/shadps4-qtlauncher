// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QProgressDialog>

#if WIN32
#include "common/bitlocker.h"
#endif

#include "common/path_util.h"
#include "compatibility_info.h"
#include "core/emulator_settings.h"
#include "core/file_sys/game_backend.h"
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

#if _WIN32
    QFileInfo dirInfo(dir);
    if (!dirInfo.isReadable() && !alreadyAskedBitlocker) {
        QString drive = dir.split(":").first();

        std::wstring wDrive = drive.toStdWString();
        PCWSTR drivePtr = wDrive.c_str();

        FveInit();

        if (FveIsLocked(drivePtr)) {
        prompt:
            bool ok;
            QString key = QInputDialog::getText(nullptr, QObject::tr("Drive Locked"),
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
                goto prompt;
            }
        }

        FveCleanup();
    }
#endif

    QFileInfoList entries = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    entries.append(
        directory.entryInfoList(QStringList{"*.zar"}, QDir::Files | QDir::NoDotAndDotDot));

    for (const auto& entry : entries) {
        if (entry.completeBaseName().endsWith("-UPDATE") ||
            entry.completeBaseName().endsWith("-patch")) {
            continue;
        }

        const bool is_zar = entry.fileName().endsWith(".zar");
        const auto entry_path = Common::FS::PathFromQString(entry.absoluteFilePath());
        if (is_zar && !Core::FileSys::IsZArchiveFile(entry_path)) {
            continue;
        }

        // Check if the folder/archive has a param.sfo and eboot.bin
        if (Core::FileSys::Exists(entry_path, "sce_sys/param.sfo") ||
            Core::FileSys::Exists(entry_path, "eboot.bin")) {
            // Check the param.sfo to see what type of dump this is
            PSF psf;
            const auto psf_data = Core::FileSys::ReadGameFile(entry_path, "sce_sys/param.sfo");
            if (psf_data && psf.Open(*psf_data)) {
                const auto category = psf.GetString("CATEGORY");
                if (category && category->substr(0, 3) == "gd") {
                    // If this is a game directory, add it to the list
                    filePaths.append(entry.absoluteFilePath());
                }
            }
        } else if (!is_zar) {
            // If this is a folder, but not a game directory, scan this folder for dumps
            ScanDirectoryRecursively(entry.absoluteFilePath(), filePaths, current_depth + 1);
        }
    }
}

GameInfoClass::GameInfoClass() = default;
GameInfoClass::~GameInfoClass() = default;

void GameInfoClass::GetGameInfo(QWidget* parent, bool force_size_refresh,
                                const std::string& force_size_serial) {
    QStringList filePaths;
    for (const auto& installLoc : EmulatorSettings.GetGameInstallDirs()) {
        QString installDir;
        Common::FS::PathToQString(installDir, installLoc);
        ScanDirectoryRecursively(installDir, filePaths, 0);
    }

    m_games = QtConcurrent::mapped(filePaths, [&](const QString& path) {
                  return readGameInfo(Common::FS::PathFromQString(path));
              }).results();

    // Progress bar, please be patient :)
    QProgressDialog dialog(tr("Loading game list, please wait :3"), tr("Cancel"), 0, 0, parent);
    dialog.setWindowTitle(tr("Loading..."));

    QFutureWatcher<void> futureWatcher;
    futureWatcher.setFuture(
        QtConcurrent::map(m_games, [force_size_refresh, force_size_serial](GameInfo& game) {
            GameListUtils::GetFolderSize(
                game, force_size_refresh ||
                          (!force_size_serial.empty() && game.serial == force_size_serial));
        }));
    connect(&futureWatcher, &QFutureWatcher<void>::finished, [&]() {
        dialog.reset();
        std::sort(m_games.begin(), m_games.end(), CompareStrings);
        // Grid searches must use the calculated sizes and tooltips too.
        m_games_backup = m_games;
    });
    connect(&dialog, &QProgressDialog::canceled, &futureWatcher, &QFutureWatcher<void>::cancel);
    dialog.setRange(0, m_games.size());
    connect(&futureWatcher, &QFutureWatcher<void>::progressValueChanged, &dialog,
            &QProgressDialog::setValue);

    dialog.exec();
}
