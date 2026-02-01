// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <QString>
#include <QStringList>

namespace QtGui::Cli {

struct Options {
    bool has_command_line_argument = false;
    bool has_emulator_argument = false;
    bool show_gui = false;
    bool no_ipc = false;
    bool open_target_specified = false;
    bool has_emulator_args_separator = false;
    std::string emulator;
    QString game_arg;
    QString open_target;
    QStringList emulator_args;
    std::optional<std::filesystem::path> normalized_game_path;
};

struct ParseResult {
    bool success = false;
    bool show_help = false;
    QString error_message;
    QString help_text;
    Options options;
};

ParseResult ParseOptions(const QStringList& arguments);

} // namespace QtGui::Cli
