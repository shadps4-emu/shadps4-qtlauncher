// SPDX-FileCopyrightText: Copyright 2025 shadPS4-Qtlauncher Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/types.h"
#include "core/user_manager.h"

using json = nlohmann::json;

struct GameInstallDir {
    std::filesystem::path path;
    bool enabled = true;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GameInstallDir, path, enabled)

struct GeneralSettings {
    std::vector<GameInstallDir> install_dirs;
    std::filesystem::path addon_install_dir;
    std::filesystem::path home_dir;
    std::filesystem::path sys_modules_dir;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GeneralSettings, install_dirs, addon_install_dir, home_dir,
                                   sys_modules_dir)

struct Debug {
    bool separate_logging_enabled = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Debug, separate_logging_enabled)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(User, user_id, user_color, user_name, controller_port)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Users, default_user_id, user)

class EmulatorSettings {
public:
    static std::shared_ptr<EmulatorSettings> GetInstance();
    static void SetInstance(std::shared_ptr<EmulatorSettings> instance);

    EmulatorSettings(const EmulatorSettings&) = delete;
    EmulatorSettings& operator=(const EmulatorSettings&) = delete;
    EmulatorSettings(EmulatorSettings&&) = delete;
    EmulatorSettings& operator=(EmulatorSettings&&) = delete;

    EmulatorSettings();
    ~EmulatorSettings();

    // General Settings
    bool AddGameInstallDir(const std::filesystem::path& dir, bool enabled = true);
    void RemoveGameInstallDir(const std::filesystem::path& dir);
    std::vector<std::filesystem::path> GetGameInstallDirs() const;
    void SetAllGameInstallDirs(const std::vector<GameInstallDir>& dirs_config);
    std::filesystem::path GetAddonInstallDir() const;
    void SetAddonInstallDir(const std::filesystem::path& dir);
    std::filesystem::path GetHomeDir() const;
    void SetHomeDir(const std::filesystem::path& dir);
    std::filesystem::path GetSysModulesDir() const;
    void SetSysModulesDir(const std::filesystem::path& dir);

    // Debug
    bool IsSeparateLoggingEnabled() const;
    void SetSeparateLoggingEnabled(bool enabled);

    // Persistence
    bool Save() const;
    bool Load();

    // Users
    UserManager& GetUserManager();
    const UserManager& GetUserManager() const;

private:
    void ResetToDefaults();

    GeneralSettings m_general{};
    Debug m_debug{};
    UserManager m_userManager{};

    static std::shared_ptr<EmulatorSettings> s_instance;
    static std::mutex s_mutex;

    std::filesystem::path GetConfigPath() const;
};
