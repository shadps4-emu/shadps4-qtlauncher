// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <set>
#include <fmt/core.h>

#include "common/config.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#ifdef ENABLE_QT_GUI
#include <QtCore>
#endif
#include "common/assert.h"
#include "common/elf_info.h"
#include "common/memory_patcher.h"
#include "common/ntapi.h"
#include "common/path_util.h"
#include "common/polyfill_thread.h"
#include "common/scm_rev.h"
#include "common/singleton.h"
#include "core/file_format/psf.h"
#include "core/file_format/trp.h"
#include "emulator.h"

// Frontend::WindowSDL* g_window = nullptr;

namespace Core {

Emulator::Emulator() {
    // Initialize NT API functions and set high priority
#ifdef _WIN32
    Common::NtApi::Initialize();
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif
}

Emulator::~Emulator() {
    const auto config_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::saveMainWindow(config_dir / "config.toml");
}

void Emulator::Run(const std::filesystem::path& file, const std::vector<std::string> args) {

    std::quick_exit(0);
}

void Emulator::LoadSystemModules(const std::string& game_serial) {}

#ifdef ENABLE_QT_GUI
void Emulator::UpdatePlayTime(const std::string& serial) {
    const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    QString filePath = QString::fromStdString((user_dir / "play_time.txt").string());

    QFile file(filePath);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        LOG_INFO(Loader, "Error opening play_time.txt");
        return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    int totalSeconds = duration.count();

    QTextStream in(&file);
    QStringList lines;
    QString content;
    while (!in.atEnd()) {
        content += in.readLine() + "\n";
    }
    file.close();

    QStringList existingLines = content.split('\n', Qt::SkipEmptyParts);
    int accumulatedSeconds = 0;
    bool found = false;

    for (const QString& line : existingLines) {
        QStringList parts = line.split(' ');
        if (parts.size() == 2 && parts[0] == QString::fromStdString(serial)) {
            QStringList timeParts = parts[1].split(':');
            if (timeParts.size() == 3) {
                int hours = timeParts[0].toInt();
                int minutes = timeParts[1].toInt();
                int seconds = timeParts[2].toInt();
                accumulatedSeconds = hours * 3600 + minutes * 60 + seconds;
                found = true;
                break;
            }
        }
    }
    accumulatedSeconds += totalSeconds;
    int hours = accumulatedSeconds / 3600;
    int minutes = (accumulatedSeconds % 3600) / 60;
    int seconds = accumulatedSeconds % 60;
    QString playTimeSaved = QString::number(hours) + ":" +
                            QString::number(minutes).rightJustified(2, '0') + ":" +
                            QString::number(seconds).rightJustified(2, '0');

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        bool lineUpdated = false;

        for (const QString& line : existingLines) {
            if (line.startsWith(QString::fromStdString(serial))) {
                out << QString::fromStdString(serial) + " " + playTimeSaved + "\n";
                lineUpdated = true;
            } else {
                out << line << "\n";
            }
        }

        if (!lineUpdated) {
            out << QString::fromStdString(serial) + " " + playTimeSaved + "\n";
        }
    }
    LOG_INFO(Loader, "Playing time for {}: {}", serial, playTimeSaved.toStdString());
}
#endif

} // namespace Core
