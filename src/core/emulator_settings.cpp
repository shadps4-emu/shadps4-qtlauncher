// SPDX-FileCopyrightText: Copyright 2025 shadPS4-Qtlauncher Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <iomanip>
#include <iostream>
#include <common/path_util.h>
#include <nlohmann/json.hpp>
#include "emulator_settings.h"

std::shared_ptr<EmulatorSettings> EmulatorSettings::s_instance = nullptr;
std::mutex EmulatorSettings::s_mutex;

using json = nlohmann::json;

// Add support for std::filesystem::path in JSON
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

EmulatorSettings::EmulatorSettings() {
    Load();
}

EmulatorSettings::~EmulatorSettings() {
    Save();
}

// --- General Settings ---
bool EmulatorSettings::AddGameInstallDir(const std::filesystem::path& dir, bool enabled) {
    for (const auto& install_dir : m_general.install_dirs) {
        if (install_dir.path == dir)
            return false;
    }
    m_general.install_dirs.push_back({dir, enabled});
    return true;
}
void EmulatorSettings::RemoveGameInstallDir(const std::filesystem::path& dir) {
    auto iterator =
        std::find_if(m_general.install_dirs.begin(), m_general.install_dirs.end(),
                     [&dir](const GameInstallDir& install_dir) { return install_dir.path == dir; });
    if (iterator != m_general.install_dirs.end()) {
        m_general.install_dirs.erase(iterator);
    }
}

const std::vector<std::filesystem::path> EmulatorSettings::GetGameInstallDirs() {
    std::vector<std::filesystem::path> enabled_dirs;
    for (const auto& dir : m_general.install_dirs)
        if (dir.enabled)
            enabled_dirs.push_back(dir.path);
    return enabled_dirs;
}

void EmulatorSettings::SetAllGameInstallDirs(const std::vector<GameInstallDir>& dirs_config) {
    m_general.install_dirs = dirs_config;
}

std::filesystem::path EmulatorSettings::GetAddonInstallDir() {
    return m_general.addon_install_dir;
}

void EmulatorSettings::SetAddonInstallDir(const std::filesystem::path& dir) {
    m_general.addon_install_dir = dir;
}

std::filesystem::path EmulatorSettings::GetHomeDir() {
    if (m_general.home_dir.empty()) {
        return Common::FS::GetUserPath(Common::FS::PathType::HomeDir);
    }
    return m_general.home_dir;
}

void EmulatorSettings::SetHomeDir(const std::filesystem::path& dir) {
    m_general.home_dir = dir;
}

std::filesystem::path EmulatorSettings::GetSysModulesDir() {
    if (m_general.sys_modules_dir.empty()) {
        return Common::FS::GetUserPath(Common::FS::PathType::SysModuleDir);
    }
    return m_general.sys_modules_dir;
}

void EmulatorSettings::SetSysModulesDir(const std::filesystem::path& dir) {
    m_general.sys_modules_dir = dir;
}

// --- Persistence ---
bool EmulatorSettings::Save() const {
    const std::filesystem::path config_path =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "config.json";

    try {
        json j;
        j["general"] = m_general;
        j["users"] = m_userManager.GetUsers();
        j["debug"] = m_debug;

        std::ofstream out(config_path);
        if (!out.is_open()) {
            std::cerr << "Failed to open file for writing: " << config_path << std::endl;
            return false;
        }

        out << std::setw(4) << j;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving settings: " << e.what() << std::endl;
        return false;
    }
}

bool EmulatorSettings::Load() {
    const std::filesystem::path config_path =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "config.json";

    try {
        std::ifstream in(config_path);
        if (!in.is_open()) {
            std::cerr << "Settings file not found: " << config_path << std::endl;
            m_userManager.GetUsers().user = m_userManager.CreateDefaultUser();
            return false;
        }

        json j;
        in >> j;

        if (j.contains("general"))
            m_general = j.at("general").get<GeneralSettings>();

        if (j.contains("debug"))
            m_debug = j.at("debug").get<Debug>();

        if (j.contains("users"))
            m_userManager.GetUsers() = j.at("users").get<Users>();
        else
            m_userManager.GetUsers().user = m_userManager.CreateDefaultUser();

        if (m_userManager.GetUsers().user.empty())
            m_userManager.GetUsers().user = m_userManager.CreateDefaultUser();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading settings: " << e.what() << std::endl;
        return false;
    }
}
