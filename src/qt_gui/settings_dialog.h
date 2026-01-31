// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <QDialog>
#include <QGroupBox>
#include <QPushButton>
#include <toml.hpp>

#include "common/config.h"
#include "common/path_util.h"
#include "gui_settings.h"
#include "ipc/ipc_client.h"
#include "qt_gui/compatibility_info.h"

namespace Ui {
class SettingsDialog;
}

class QAbstractButton;
class QDialogButtonBox;
class QTabWidget;
class QTextEdit;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(std::shared_ptr<gui_settings> gui_settings,
                            std::shared_ptr<CompatibilityInfoClass> m_compat_info,
                            std::shared_ptr<IpcClient> ipc_client, QWidget* parent = nullptr,
                            bool is_game_running = false, bool is_game_specific = false,
                            std::string gsc_serial = "");
    ~SettingsDialog();

    QTabWidget* GetTabWidget() const;
    QDialogButtonBox* GetButtonBox() const;
    QTextEdit* GetDescriptionText() const;
    void SetCloseTarget(QWidget* close_target);
    void SetSingleTabScope(const QString& tab_object_name);
    void ClearTabScope();

    bool eventFilter(QObject* obj, QEvent* event) override;
    void updateNoteTextEdit(const QString& groupName);

    int exec() override;

signals:
    void LanguageChanged(const QString& locale);
    void CompatibilityChanged();
    void BackgroundOpacityChanged(int opacity);

private:
    enum class SettingsTabId {
        General,
        Gui,
        Graphics,
        User,
        Input,
        Paths,
        Log,
        Debug,
        Experimental,
        Unknown,
    };

    static SettingsTabId TabIdFromObjectName(const QString& object_name);
    static bool ShouldApplyToTab(SettingsTabId tab_id, SettingsTabId scope_tab_id);

    void HandleButtonBoxClicked(QAbstractButton* button);
    void ApplySettingsForScope();
    void ApplySettingsForTab(SettingsTabId tab_id);
    void LoadValuesForScope(const toml::value& data, const toml::value& gs_data,
                            bool is_newly_created);
    void LoadValuesForTab(SettingsTabId tab_id, const toml::value& data,
                          const toml::value& gs_data, bool is_newly_created);
    void SetDefaultValuesForScope();
    void SetDefaultValuesForTab(SettingsTabId tab_id);
    void SyncRealTimeWidgetsForScope(const toml::value& data, const toml::value& gs_data);
    void SyncRealTimeWidgetsForTab(SettingsTabId tab_id, const toml::value& data,
                                   const toml::value& gs_data);
    void SyncGameFoldersFromConfig(const toml::value& data);
    void SyncVolumeFromConfig(const toml::value& gs_data);
    void SyncRunningGameGpuFromConfig(const toml::value& gs_data);
    bool LoadSyncConfigData(toml::value& data, toml::value& gs_data) const;

    void LoadValuesFromConfig();
    void InitializeEmulatorLanguages();
    void OnLanguageChanged(int index);
    void OnCursorStateChanged(s16 index);
    void closeEvent(QCloseEvent* event) override;
    void VolumeSliderChange(int value);
    void onAudioDeviceChange(bool isAdd);
    void pollSDLevents();
    void getPhysicalDevices();

    std::unique_ptr<Ui::SettingsDialog> ui;

    std::map<std::string, int> languages;

    QString defaultTextEdit;

    int initialHeight;

    std::string gs_serial;

    std::filesystem::path config_file;

    SettingsTabId scoped_tab_id = SettingsTabId::Unknown;
    QWidget* close_target = nullptr;

    bool is_game_running = false;
    bool is_game_specific = false;
    bool is_game_saving = false;

    std::shared_ptr<gui_settings> m_gui_settings;
    std::shared_ptr<IpcClient> m_ipc_client;
    QFuture<void> Polling;
};
