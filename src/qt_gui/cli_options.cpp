// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCommandLineOption>
#include <QCommandLineParser>

#include "common/path_util.h"
#include "qt_gui/cli_options.h"

namespace QtGui::Cli {

namespace {

struct ArgumentSplit {
    QStringList option_args;
    QStringList emulator_args;
    bool has_separator = false;
};

ArgumentSplit SplitArguments(const QStringList& arguments) {
    ArgumentSplit split;
    split.option_args = arguments;
    const int separator_index = split.option_args.indexOf("--");
    if (separator_index < 0) {
        return split;
    }

    split.has_separator = true;
    split.emulator_args = split.option_args.mid(separator_index + 1);
    split.option_args = split.option_args.mid(0, separator_index);
    return split;
}

std::optional<std::filesystem::path> NormalizeGamePath(const QString& path) {
    if (path.isEmpty()) {
        return std::nullopt;
    }

    std::filesystem::path fs_path;
#ifdef _WIN32
    fs_path = std::filesystem::path(path.toStdWString());
#else
    fs_path = std::filesystem::path(path.toStdString());
#endif
    return Common::FS::NormalizeGamePathToEboot(fs_path);
}

QString PathToQString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

} // namespace

ParseResult ParseOptions(const QStringList& arguments) {
    ParseResult result;
    result.options.has_command_line_argument = arguments.size() > 1;

    const ArgumentSplit split = SplitArguments(arguments);
    result.options.emulator_args = split.emulator_args;
    result.options.has_emulator_args_separator = split.has_separator;

    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.addHelpOption();

    const QCommandLineOption open_option("open", "Open a UI target and exit when closed.",
                                         "target");
    const QCommandLineOption game_option(
        QStringList{"g", "game"}, "Game path (eboot.bin or directory containing it).", "path");
    const QCommandLineOption emulator_option(QStringList{"e", "emulator"}, "Emulator name or path.",
                                             "name_or_path");
    const QCommandLineOption default_emulator_option("d", "Alias for '--emulator default'.");
    const QCommandLineOption show_gui_option(QStringList{"s", "show-gui"},
                                             "Show the GUI after launch.");
    const QCommandLineOption no_ipc_option(QStringList{"i", "no-ipc"}, "Disable IPC.");

    parser.addOption(open_option);
    parser.addOption(game_option);
    parser.addOption(emulator_option);
    parser.addOption(default_emulator_option);
    parser.addOption(show_gui_option);
    parser.addOption(no_ipc_option);

    if (!parser.parse(split.option_args)) {
        result.error_message = parser.errorText();
        return result;
    }

    if (parser.isSet("help")) {
        result.show_help = true;
        result.help_text = parser.helpText();
        result.success = true;
        return result;
    }

    if (parser.isSet(default_emulator_option) && parser.isSet(emulator_option)) {
        result.error_message = "--emulator and -d cannot be used together.";
        return result;
    }

    result.options.show_gui = parser.isSet(show_gui_option);
    result.options.no_ipc = parser.isSet(no_ipc_option);

    if (parser.isSet(emulator_option)) {
        result.options.emulator = parser.value(emulator_option).toStdString();
        result.options.has_emulator_argument = true;
    } else if (parser.isSet(default_emulator_option)) {
        result.options.emulator = "default";
        result.options.has_emulator_argument = true;
    }

    if (parser.isSet(game_option)) {
        result.options.game_arg = parser.value(game_option);
        auto normalized = NormalizeGamePath(result.options.game_arg);
        if (!normalized) {
            result.error_message = "Invalid game path provided.";
            return result;
        }
        result.options.normalized_game_path = normalized;
        result.options.game_arg = PathToQString(*normalized);
    }

    if (parser.isSet(open_option)) {
        result.options.open_target_specified = true;
        result.options.open_target = parser.value(open_option);
    }

    if (result.options.open_target_specified) {
        if (result.options.has_emulator_argument || result.options.show_gui ||
            result.options.no_ipc || !result.options.emulator_args.isEmpty()) {
            result.error_message =
                "--open cannot be combined with emulator launch options or '--' arguments.";
            return result;
        }
    }

    if (!result.options.open_target_specified && result.options.has_command_line_argument &&
        !result.options.has_emulator_argument) {
        result.error_message = "Please provide a name or path for the emulator core.";
        return result;
    }

    if (!result.options.open_target_specified && !result.options.has_emulator_argument &&
        !result.options.game_arg.isEmpty()) {
        result.error_message = "--game requires --emulator (or -d).";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace QtGui::Cli
