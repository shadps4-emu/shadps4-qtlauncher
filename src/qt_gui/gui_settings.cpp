// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/path_util.h"
#include "gui_settings.h"

gui_settings::gui_settings(QObject* parent) : settings(parent) {
    m_settings = std::make_unique<QSettings>(ComputeSettingsDir() + "qt_ui.ini",
                                             QSettings::Format::IniFormat, parent);
}

QString gui_settings::GetVersionExecutablePath(const QString& versionName) const {
    const auto versionsFolder =
        Common::FS::PathFromQString(GetValue(gui::vm_versionPath).toString());
    const auto versionFolder = versionsFolder / Common::FS::PathFromQString(versionName);

    std::string exeName;
#ifdef Q_OS_WIN
    exeName = "shadPS4.exe";
#elif defined(Q_OS_LINUX)
    exeName = "Shadps4-sdl.AppImage";
#elif defined(Q_OS_MACOS)
    exeName = "shadps4";
#endif

    QString result;
    Common::FS::PathToQString(result, versionFolder / exeName);
    return result;
}
