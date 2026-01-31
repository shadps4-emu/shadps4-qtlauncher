// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/config.h"
#include "common/logging/backend.h"
#include "common/versions.h"
#include "core/emulator_state.h"
#include "qt_gui/cli_options.h"
#include "qt_gui/game_install_dialog.h"
#include "qt_gui/main_window.h"
#include "qt_gui/open_targets.h"

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
    std::shared_ptr<EmulatorState> m_emu_state = std::make_shared<EmulatorState>();
    EmulatorState::SetInstance(m_emu_state);

    // Load configurations and initialize Qt application
    const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::load(user_dir / "config.toml");

    // Ignore Qt logs
    qInstallMessageHandler(customMessageHandler);

    const auto parse_result = QtGui::Cli::ParseOptions(QCoreApplication::arguments());
    if (!parse_result.success) {
        std::cerr << "Error: " << parse_result.error_message.toStdString() << "\n";
        return 1;
    }
    if (parse_result.show_help) {
        std::cout << parse_result.help_text.toStdString();
        return 0;
    }
    const auto& options = parse_result.options;

    // If no game directories are set and no command line argument, prompt for it
    if (Config::getGameInstallDirsEnabled().empty() && !options.has_command_line_argument &&
        !options.open_target_specified) {
        GameInstallDialog dlg;
        dlg.exec();
    }

    Common::Log::Initialize("shadPS4Launcher.log");

    // Initialize the main window
    MainWindow* m_main_window = new MainWindow(nullptr);
    if (options.open_target_specified) {
        m_main_window->Init(MainWindow::InitMode::Headless);
        QTimer::singleShot(0, [m_main_window, options]() {
            auto context = m_main_window->BuildOpenTargetContext(nullptr);
            context.game_path = options.normalized_game_path;
            auto result = UiOpenTargets::OpenTarget(options.open_target, context,
                                                    UiOpenTargets::OpenBehaviorForCli());
            if (!result.success) {
                QCoreApplication::exit(1);
            }
        });
        return a.exec();
    }

    if ((options.has_command_line_argument && options.show_gui) ||
        !options.has_command_line_argument) {
        m_main_window->Init(MainWindow::InitMode::Full);
    }
    if (options.has_emulator_argument) {
        const std::string& emulator = options.emulator;
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
        if (!options.show_gui) {
            m_main_window->m_ipc_client->gameClosedFunc = StopProgram;
        }
        m_main_window->StartEmulatorExecutable(emulator_path, options.game_arg,
                                               options.emulator_args, options.no_ipc);
    }

    if (!options.has_emulator_argument || options.show_gui) {
        m_main_window->show();
    }
    return a.exec();
}
