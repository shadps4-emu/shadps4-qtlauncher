// SPDX-FileCopyrightText: Copyright 2025 shadPS4-Qtlauncher Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <iomanip>
#include <iostream>
#include <common/path_util.h>
#include <nlohmann/json.hpp>
#include "emulator_settings.h"

using json = nlohmann::json;

EmulatorSettings::EmulatorSettings() {
    Load();
}

EmulatorSettings::~EmulatorSettings() {
    Save();
}

bool EmulatorSettings::Save() const {
    const std::filesystem::path config_path =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "config.json";

    try {
        json j;
        j["users"] = m_userManager.GetUsers();

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