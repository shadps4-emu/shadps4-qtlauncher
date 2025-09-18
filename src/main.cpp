// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iostream"
#include "system_error"
#include "unordered_map"

#include "common/config.h"
#include "common/memory_patcher.h"
#include "common/logging/backend.h"
#include "qt_gui/game_install_dialog.h"
#include "qt_gui/main_window.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "emulator.h"
// Custom message handler to ignore Qt logs
void customMessageHandler(QtMsgType, const QMessageLogContext&, const QString&) {}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    QApplication a(argc, argv);

    QApplication::setDesktopFileName("net.shadps4.qtlauncher");

    // Load configurations and initialize Qt application
    const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::load(user_dir / "config.toml");

    bool has_command_line_argument = argc > 1;
    bool show_gui = false, has_game_argument = false;
    std::string game_path;
    std::vector<std::string> game_args{};

    // If no game directories are set and no command line argument, prompt for it
    if (Config::getGameInstallDirsEnabled().empty() && !has_command_line_argument) {
        GameInstallDialog dlg;
        dlg.exec();
    }

    // Ignore Qt logs
    qInstallMessageHandler(customMessageHandler);

    Common::Log::Initialize("shadPS4Launcher.log");

    // Initialize the main window
    MainWindow* m_main_window = new MainWindow(nullptr);

    m_main_window->Init();

    m_main_window->show();
    return a.exec();
}
