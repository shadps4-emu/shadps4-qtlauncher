// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iostream"
#include "system_error"
#include "unordered_map"

#include "common/config.h"
#include "common/logging/backend.h"
#include "common/versions.h"
#include "core/emulator_settings.h"
#include "qt_gui/game_install_dialog.h"
#include "qt_gui/main_window.h"
#ifdef _WIN32
#include <windows.h>
#endif

// Custom message handler to ignore Qt logs
void customMessageHandler(QtMsgType, const QMessageLogContext&, const QString&) {}

void StopProgram() {
    exit(0);
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    QApplication a(argc, argv);

    QApplication::setDesktopFileName("net.shadps4.qtlauncher");

    // Load configurations and initialize Qt application
    const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::load(user_dir / "config.toml");

    const bool has_command_line_argument = argc > 1;
    bool has_emulator_argument = false;
    bool show_gui = false, no_ipc = false;
    std::string emulator;
    QStringList emulator_args{};
    QString game_arg = "";
    std::shared_ptr<EmulatorSettings> emu_settings = std::make_shared<EmulatorSettings>();

    // Ignore Qt logs
    qInstallMessageHandler(customMessageHandler);

    // Map of argument strings to lambda functions
    std::unordered_map<std::string, std::function<void(int&)>> arg_map = {
        {"-h",
         [&](int&) {
             std::cout
                 << "Usage: shadps4 [options]\n"
                    "Options:\n"
                    "  No arguments: Opens the GUI.\n"
                    "  -e, --emulator <name|path>    Specify the emulator version/path you want to "
                    "use, or 'default' for using the version selected in the config.\n"
                    "  -g, --game <ID|path>          Specify game to launch.\n"
                    "  -d                            Alias for '-e default'.\n"
                    " -- ...                         Parameters passed to the emulator core. "
                    "Needs to be at the end of the line, and everything after '--' is an "
                    "emulator argument.\n"
                    "  -s, --show-gui                Show the GUI.\n"
                    "  -i, --no-ipc                  Disable IPC.\n"
                    "  -h, --help                    Display this help message.\n";
             exit(0);
         }},
        {"--help", [&](int& i) { arg_map["-h"](i); }}, // Redirect --help to -h

        {"-s", [&](int&) { show_gui = true; }},
        {"--show-gui", [&](int& i) { arg_map["-s"](i); }},
        {"-i", [&](int&) { no_ipc = true; }},
        {"--no-ipc", [&](int& i) { arg_map["-i"](i); }},

        {"-g",
         [&](int& i) {
             if (i + 1 < argc) {
                 game_arg = argv[++i];
             } else {
                 std::cerr << "Error: Missing argument for -g/--game\n";
                 exit(1);
             }
         }},
        {"--game", [&](int& i) { arg_map["-g"](i); }},
        {"-e",
         [&](int& i) {
             if (i + 1 < argc) {
                 emulator = argv[++i];
                 has_emulator_argument = true;
             } else {
                 std::cerr << "Error: Missing argument for -e/--emulator\n";
                 exit(1);
             }
         }},
        {"--emulator", [&](int& i) { arg_map["-e"](i); }},
        {"-d",
         [&](int&) {
             emulator = "default";
             has_emulator_argument = true;
         }},
    };

    // Parse command-line arguments using the map
    for (int i = 1; i < argc; ++i) {
        std::string cur_arg = argv[i];
        auto it = arg_map.find(cur_arg);
        if (it != arg_map.end()) {
            it->second(i); // Call the associated lambda function
        } else if (i == argc - 1 && !has_emulator_argument) {
            // Assume the last argument is the game file if not specified via -g/--game
            emulator = argv[i];
            has_emulator_argument = true;
        } else if (std::string(argv[i]) == "--") {
            if (i + 1 == argc) {
                std::cerr << "Warning: -- is set, but no emulator arguments are added!\n";
                break;
            }
            for (int j = i + 1; j < argc; j++) {
                emulator_args.push_back(argv[j]);
            }
            break;
        } else if (i + 1 < argc && std::string(argv[i + 1]) == "--") {
            if (!has_emulator_argument) {
                emulator = argv[i];
                has_emulator_argument = true;
            }
        } else {
            std::cerr << "Unknown argument: " << cur_arg << ", see --help for info.\n";
            return 1;
        }
    }

    // If no game directories are set and no command line argument, prompt for it
    if (Config::getGameInstallDirsEnabled().empty() && !has_command_line_argument) {
        GameInstallDialog dlg;
        dlg.exec();
    }

    Common::Log::Initialize("shadPS4Launcher.log");

    if (has_command_line_argument && !has_emulator_argument) {
        std::cerr << "Error: Please provide a name or path for the emulator core.\n";
        exit(1);
    }

    // Initialize the main window
    MainWindow* m_main_window = new MainWindow(emu_settings,nullptr);
    if ((has_command_line_argument && show_gui) || !has_command_line_argument) {
        m_main_window->Init();
    }
    if (has_emulator_argument) {
        std::filesystem::path emulator_path;
        if (std::filesystem::exists(emulator)) {
            emulator_path = emulator;
        } else if (emulator == "default") {
            gui_settings settings{};
            emulator_path = settings.GetValue(gui::vm_versionSelected).toString().toStdString();
        } else {
            auto const& versions = VersionManager::GetVersionList();
            for (auto const& v : versions) {
                if (v.name == emulator) {
                    emulator_path = v.path;
                    break;
                }
            }
        }
        if (!std::filesystem::exists(emulator_path)) {
            std::cerr << "Error: specified emulator name or path is not found.\n";
            return 1;
        }
        if (!show_gui) {
            m_main_window->m_ipc_client->gameClosedFunc = StopProgram;
        }
        m_main_window->StartEmulatorExecutable(emulator_path, game_arg, emulator_args, no_ipc);
    }

    if (!has_emulator_argument || show_gui) {
        m_main_window->show();
    }
    return a.exec();
}
