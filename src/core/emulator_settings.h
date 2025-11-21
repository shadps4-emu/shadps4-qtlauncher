// SPDX-FileCopyrightText: Copyright 2025 shadPS4-Qtlauncher Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include "common/types.h"
#include "core/user_manager.h"

struct GameInstallDir {
    std::filesystem::path path;
    bool enabled;
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

// Keep JSON type definitions for User and Users centralized here
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(User, user_id, user_color, user_name, controller_port)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Users, default_user_id, user)

class EmulatorSettings {
public:
    EmulatorSettings();
    ~EmulatorSettings();

    // --- Singleton access ---
    static std::shared_ptr<EmulatorSettings> GetInstance() {
        std::lock_guard<std::mutex> lock(s_mutex);

        if (!s_instance) {
            // Lazy initialization if no existing instance
            s_instance = std::shared_ptr<EmulatorSettings>(new EmulatorSettings());
        }
        return s_instance;
    }

    // set an existing in-memory object as the singleton
    static void SetInstance(std::shared_ptr<EmulatorSettings> instance) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_instance = instance;
    }

    // General Settings
    bool AddGameInstallDir(const std::filesystem::path& dir, bool enabled = true);
    void RemoveGameInstallDir(const std::filesystem::path& dir);
    const std::vector<std::filesystem::path> GetGameInstallDirs();
    void SetAllGameInstallDirs(const std::vector<GameInstallDir>& dirs_config);
    std::filesystem::path GetAddonInstallDir();
    void SetAddonInstallDir(const std::filesystem::path& dir);
    std::filesystem::path GetHomeDir();
    void SetHomeDir(const std::filesystem::path& dir);
    std::filesystem::path GetSysModulesDir();
    void SetSysModulesDir(const std::filesystem::path& dir);
    // Debug Settings
    bool IsSeparateLoggingEnabled() const {
        return m_debug.separate_logging_enabled;
    }
    void SetSeparateLoggingEnabled(bool enabled) {
        m_debug.separate_logging_enabled = enabled;
    }

    bool Save() const;
    bool Load();

    UserManager& GetUserManager() {
        return m_userManager;
    }
    const UserManager& GetUserManager() const {
        return m_userManager;
    }

private:
    GeneralSettings m_general = {};
    Debug m_debug = {};
    UserManager m_userManager;
    static std::shared_ptr<EmulatorSettings> s_instance;
    static std::mutex s_mutex;
};