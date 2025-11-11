// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWindow>
#include "settings.h"

namespace gui {
// categories
const QString general_settings = "general_settings";
const QString main_window = "main_window";
const QString game_list = "game_list";
const QString game_list_column = "game_list_column";
const QString game_grid = "game_grid";
const QString favorites = "favorites";
const QString version_manager = "version_manager";

// general
const gui_value gen_checkForUpdates = gui_value(general_settings, "checkForUpdates", false);
const gui_value gen_showChangeLog = gui_value(general_settings, "showChangeLog", false);
const gui_value gen_recentFiles =
    gui_value(main_window, "recentFiles", QVariant::fromValue(QList<QString>()));
const gui_value gen_guiLanguage = gui_value(general_settings, "guiLanguage", "en_US");
const gui_value gen_elfDirs =
    gui_value(main_window, "elfDirs", QVariant::fromValue(QList<QString>()));
const gui_value gen_theme = gui_value(general_settings, "theme", 0);
const gui_value gen_shadPath = gui_value(general_settings, "shadPath", "");
const gui_value gen_checkCompatibilityAtStartup =
    gui_value(general_settings, "checkCompatibilityAtStartup", false);
const gui_value gen_homeTab = gui_value(general_settings, "homeTab", "General");

// main window settings
const gui_value mw_geometry = gui_value(main_window, "geometry", QByteArray());
const gui_value mw_showLabelsUnderIcons = gui_value(main_window, "showLabelsUnderIcons", true);
const gui_value mw_showLog = gui_value(main_window, "showLog", true);
const gui_value mw_dockWidgetSizes =
    gui_value(main_window, "dockWidgetSizes", QVariant::fromValue(QList<int>()));

// game list settings
const gui_value gl_mode = gui_value(game_list, "tableMode", 0);
const gui_value gl_icon_size = gui_value(game_list, "icon_size", 36);
const gui_value gl_slider_pos = gui_value(game_list, "slider_pos", 0);
const gui_value gl_showBackgroundImage = gui_value(game_list, "showBackgroundImage", true);
const gui_value gl_backgroundImageOpacity = gui_value(game_list, "backgroundImageOpacity", 50);
const gui_value gl_playBackgroundMusic = gui_value(game_list, "playBackgroundMusic", true);
const gui_value gl_backgroundMusicVolume = gui_value(game_list, "backgroundMusicVolume", 50);
const gui_value gl_VolumeSlider = gui_value(game_list, "volumeSlider", 100);

// game list (column)
const gui_value glc_showIconEnabled = gui_value(game_list_column, "showIcon", true);
const gui_value glc_showNameEnabled = gui_value(game_list_column, "showName", true);
const gui_value glc_showCompatibility = gui_value(game_list_column, "showCompatibility", false);
const gui_value glc_showSerialEnabled = gui_value(game_list_column, "showSerial", true);
const gui_value glc_showRegionEnabled = gui_value(game_list_column, "showRegion", true);
const gui_value glc_showFirmwareEnabled = gui_value(game_list_column, "showFirmware", true);
const gui_value glc_showLoadGameSizeEnabled = gui_value(game_list_column, "showLoadGameSize", true);
const gui_value glc_showVersionEnabled = gui_value(game_list_column, "showVersion", true);
const gui_value glc_showPlayTimeEnabled = gui_value(game_list_column, "showPlayTime", true);
const gui_value glc_showPathEnabled = gui_value(game_list_column, "showPath", true);
const gui_value glc_showFavoriteEnabled = gui_value(game_list_column, "showFavorite", true);

// game grid settings
const gui_value gg_icon_size = gui_value(game_grid, "icon_size", 69);
const gui_value gg_slider_pos = gui_value(game_grid, "slider_pos", 0);

// favorites list
const gui_value favorites_list =
    gui_value(favorites, "favoritesList", QVariant::fromValue(QList<QString>()));

// version manager
const gui_value vm_versionPath = gui_value(version_manager, "versionPath", "");
const gui_value vm_versionSelected = gui_value(version_manager, "versionSelected", "");
const gui_value vm_showChangeLog = gui_value(version_manager, "showChangeLog", "");
const gui_value vm_checkOnStartup = gui_value(version_manager, "checkOnStartup", "");

} // namespace gui

class gui_settings : public settings {
    Q_OBJECT

public:
    explicit gui_settings(QObject* parent = nullptr);
    ~gui_settings() override = default;

    QString GetVersionExecutablePath(const QString& versionName) const;
};
