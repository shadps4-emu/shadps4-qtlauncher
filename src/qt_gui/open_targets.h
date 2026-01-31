// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <QString>
#include <QStringList>
#include <QWidget>

class CompatibilityInfoClass;
class GameInfoClass;
class IpcClient;
class gui_settings;
class QWidget;

namespace UiOpenTargets {

enum class TargetId {
    Settings,
    VersionManager,
    Controllers,
    KeyboardMouse,
    Hotkeys,
    About,
    GameInstall,
    CheatsPatchesDownload,
    TrophyViewer,
    SkylanderPortal,
    InfinityBase,
    DimensionsToypad,
};

enum class ErrorMode {
    Default,
    Silent,
    MessageBox,
    StdErr,
};

struct OpenBehavior {
    bool exit_on_close = false;
    ErrorMode error_mode = ErrorMode::Default;
};

OpenBehavior OpenBehaviorForUi();
OpenBehavior OpenBehaviorForCli();

struct OpenTargetContext {
    std::shared_ptr<gui_settings> gui_settings;
    std::shared_ptr<CompatibilityInfoClass> compat_info;
    std::shared_ptr<IpcClient> ipc_client;
    std::shared_ptr<GameInfoClass> game_info;
    std::string running_game_serial;
    bool is_game_running = false;
    bool is_game_specific = false;
    std::string game_serial;
    std::optional<std::filesystem::path> game_path;
    bool attach_parent_destroy = false;
    QWidget* parent = nullptr;
};

struct OpenTargetResult {
    bool success = false;
    QString error_message;
    QWidget* opened_window = nullptr;
};

std::optional<TargetId> ParseTargetId(const QString& target);
QStringList GetTargetIdStrings();
OpenTargetResult OpenTarget(TargetId target_id, const OpenTargetContext& context,
                            const OpenBehavior& behavior);
OpenTargetResult OpenTarget(const QString& target, const OpenTargetContext& context,
                            const OpenBehavior& behavior);

} // namespace UiOpenTargets
