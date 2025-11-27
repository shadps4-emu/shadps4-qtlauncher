// SPDX-FileCopyrightText: Copyright 2025 shadPS4-Qtlauncher Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "emulator_settings.h"

#include <fstream>
#include <iomanip>
#include <iostream>

#include "common/path_util.h"

using json = nlohmann::json;

namespace nlohmann {
template <>
struct adl_serializer<std::filesystem::path> {
    static void to_json(json& j, const std::filesystem::path& p) {
        j = p.u8string();
    }
    static void from_json(const json& j, std::filesystem::path& p) {
        p = j.get<std::string>();
    }
};
} // namespace nlohmann

std::shared_ptr<EmulatorSettings> EmulatorSettings::s_instance = nullptr;
std::mutex EmulatorSettings::s_mutex;

EmulatorSettings::EmulatorSettings() {
    ResetToDefaults();
    Load();
}

EmulatorSettings::~EmulatorSettings() {
    if (!Save()) {
        std::cerr << "Warning: failed to save EmulatorSettings on destruction\n";
    }
}

std::shared_ptr<EmulatorSettings> EmulatorSettings::GetInstance() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_instance) {
        s_instance = std::shared_ptr<EmulatorSettings>(new EmulatorSettings());
    }
    return s_instance;
}

void EmulatorSettings::SetInstance(std::shared_ptr<EmulatorSettings> instance) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_instance = std::move(instance);
}

void EmulatorSettings::ResetToDefaults() {
    m_general = GeneralSettings{};
    m_debug = Debug{};
    m_userManager = UserManager{};
    if (m_userManager.GetUsers().user.empty())
        m_userManager.GetUsers().user = m_userManager.CreateDefaultUser();
}

bool EmulatorSettings::AddGameInstallDir(const std::filesystem::path& dir, bool enabled) {
    for (const auto& d : m_general.install_dirs)
        if (d.path == dir)
            return false;
    m_general.install_dirs.push_back({dir, enabled});
    return true;
}

void EmulatorSettings::RemoveGameInstallDir(const std::filesystem::path& dir) {
    auto it = std::find_if(m_general.install_dirs.begin(), m_general.install_dirs.end(),
                           [&dir](const GameInstallDir& d) { return d.path == dir; });
    if (it != m_general.install_dirs.end())
        m_general.install_dirs.erase(it);
}

std::vector<std::filesystem::path> EmulatorSettings::GetGameInstallDirs() const {
    std::vector<std::filesystem::path> enabled;
    for (const auto& d : m_general.install_dirs)
        if (d.enabled)
            enabled.push_back(d.path);
    return enabled;
}

void EmulatorSettings::SetAllGameInstallDirs(const std::vector<GameInstallDir>& dirs_config) {
    m_general.install_dirs = dirs_config;
}

std::filesystem::path EmulatorSettings::GetAddonInstallDir() const {
    return m_general.addon_install_dir;
}
void EmulatorSettings::SetAddonInstallDir(const std::filesystem::path& dir) {
    m_general.addon_install_dir = dir;
}

std::filesystem::path EmulatorSettings::GetHomeDir() const {
    return m_general.home_dir.empty() ? Common::FS::GetUserPath(Common::FS::PathType::HomeDir)
                                      : m_general.home_dir;
}
void EmulatorSettings::SetHomeDir(const std::filesystem::path& dir) {
    m_general.home_dir = dir;
}

std::filesystem::path EmulatorSettings::GetSysModulesDir() const {
    return m_general.sys_modules_dir.empty()
               ? Common::FS::GetUserPath(Common::FS::PathType::SysModuleDir)
               : m_general.sys_modules_dir;
}
void EmulatorSettings::SetSysModulesDir(const std::filesystem::path& dir) {
    m_general.sys_modules_dir = dir;
}

// -----------------------------
// Debug
// -----------------------------
bool EmulatorSettings::IsSeparateLoggingEnabled() const {
    return m_debug.separate_logging_enabled;
}
void EmulatorSettings::SetSeparateLoggingEnabled(bool enabled) {
    m_debug.separate_logging_enabled = enabled;
}

// -----------------------------
// Users
// -----------------------------
UserManager& EmulatorSettings::GetUserManager() {
    return m_userManager;
}
const UserManager& EmulatorSettings::GetUserManager() const {
    return m_userManager;
}

// -----------------------------
// Persistence
// -----------------------------
std::filesystem::path EmulatorSettings::GetConfigPath() const {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "config.json";
}

bool EmulatorSettings::Save() const {
    const auto config_path = GetConfigPath();
    try {
        json j;
        j["general"] = m_general;
        j["users"] = m_userManager.GetUsers();
        j["debug"] = m_debug;

        std::ofstream out(config_path, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "Failed to open file for writing: " << config_path << "\n";
            return false;
        }

        out << std::setw(4) << j;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error saving settings: " << e.what() << "\n";
        return false;
    }
}

bool EmulatorSettings::Load() {
    const auto config_path = GetConfigPath();

    if (!std::filesystem::exists(config_path)) {
        std::cout << "Settings file not found. Creating default.\n";
        return Save();
    }

    try {
        std::ifstream in(config_path, std::ios::binary);
        if (!in.is_open()) {
            std::cerr << "Failed to open config for reading: " << config_path << "\n";
            return false;
        }

        json j;
        in >> j;

        if (j.contains("general"))
            j["general"].get_to(m_general);
        if (j.contains("debug"))
            j["debug"].get_to(m_debug);
        if (j.contains("users")) {
            j["users"].get_to(m_userManager.GetUsers());
        }

        // Ensure at least one default user exists
        if (m_userManager.GetUsers().user.empty()) {
            m_userManager.GetUsers().user = m_userManager.CreateDefaultUser();
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error loading settings: " << e.what() << "\n";
        return false;
    }
}
