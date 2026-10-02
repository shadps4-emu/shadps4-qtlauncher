// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QTableWidget>

#include "background_music_player.h"
#include "compatibility_info.h"
#include "game_info.h"
#include "game_list_utils.h"
#include "gui_context_menus.h"
#include "gui_settings.h"
#include "ipc/ipc_client.h"

class GameListFrame : public QTableWidget {
    Q_OBJECT
public:
    explicit GameListFrame(std::shared_ptr<gui_settings> gui_settings,
                           std::shared_ptr<GameInfoClass> game_info_get,
                           std::shared_ptr<CompatibilityInfoClass> compat_info_get,
                           std::shared_ptr<IpcClient> ipc_client, QWidget* parent = nullptr);
Q_SIGNALS:
    void GameListFrameClosed();
    void RequestRefreshList();

public Q_SLOTS:
    void SetListBackgroundImage(QTableWidgetItem* item);
    void RefreshListBackgroundImage();
    void resizeEvent(QResizeEvent* event);
    void SortNameAscending(int columnIndex);
    void SortNameDescending(int columnIndex);
    void PlayBackgroundMusic(QTableWidgetItem* item);
    void onCurrentCellChanged(int currentRow, int currentColumn, int previousRow,
                              int previousColumn);

private:
    void SetTableItem(int row, int column, QString itemStr);
    void SetRegionFlag(int row, int column, QString itemStr);
    void UpdateFavoriteStateForAllItems();
    void UpdateFavoriteVisual(int row);
    void SetFavoriteIcon(int row, int column);
    void SetCompatibilityItem(int row, int column, CompatibilityEntry entry, int gameIndex);

    QString GetPlayTime(const std::string& serial);

    void SortTable(int columnIndex, bool ascending);
    void UpdateFavoriteCache();
    void LoadPlayTimeCache();

    QList<QAction*> m_columnActs;

    GameInfoClass* game_inf_get = nullptr;

    bool ListSortedAsc = true;
    int sortColumn = 1;

    int m_current_game_index = -1;
    int m_current_column = -1;

    int m_last_opacity = -1; // Track last opacity to avoid unnecessary recomputation
    std::filesystem::path m_current_game_path; // Track current game path to detect changes

    std::shared_ptr<gui_settings> m_gui_settings;

    QSet<QString> m_favorite_serials;
    QHash<QString, QString> m_play_time_cache;

public:
    void PopulateGameList(bool isInitialPopulation = true);
    void ResizeIcons(int iconSize);
    void ApplyLastSorting(bool isInitialPopulation);
    QTableWidgetItem* GetCurrentItem();
    void ToggleColumnVisibility();
    void ShowHeaderContextMenu(const QPoint& pos);
    QImage backgroundImage;
    GameListUtils m_game_list_utils;
    GuiContextMenus m_gui_context_menus;
    std::shared_ptr<GameInfoClass> m_game_info;
    std::shared_ptr<CompatibilityInfoClass> m_compat_info;
    std::shared_ptr<IpcClient> m_ipc_client;

    int icon_size;
    std::string last_favorite;
    int GetGameIndexForRow(int row) const;
};
