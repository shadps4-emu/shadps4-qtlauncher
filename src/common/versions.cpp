// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logging/log.h"
#include "path_util.h"
#include "versions.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace VersionManager {

std::vector<Version> GetVersionList(std::filesystem::path const& path) {
    std::filesystem::path cfg_path =
        path.empty() ? Common::FS::GetUserPath(Common::FS::PathType::LauncherDir) / "versions.json"
                     : path;

    std::ifstream ifs(cfg_path);
    if (!ifs) {
        fmt::print(stderr, "VersionManager: Config file not found: {}\n", cfg_path.string());
        return {};
    }

    json data;
    try {
        ifs >> data;
    } catch (const std::exception& ex) {
        fmt::print(stderr, "VersionManager: Failed to parse JSON: {}\n", ex.what());
        return {};
    }

    if (!data.is_array()) {
        fmt::print(stderr, "VersionManager: Invalid JSON format (expected array)\n");
        return {};
    }

    std::vector<Version> versions;
    int id = 0;

    for (const auto& entry : data) {
        if (!entry.is_object()) {
            fmt::print(stderr, "VersionManager: Skipping invalid entry (not an object)\n");
            continue;
        }

        Version v{
            .name = entry.value("name", std::string("no name")),
            .path = entry.value("path", std::string("")),
            .date = entry.value("date", std::string("never")),
            .codename = entry.value("codename", std::string("")),
            .type = static_cast<VersionType>(
                entry.value("type", static_cast<int>(VersionType::Custom))),
            .id = id++,
        };

        versions.push_back(std::move(v));
    }

    // Sort by id just for consistent ordering
    std::sort(versions.begin(), versions.end(),
              [](const Version& a, const Version& b) { return a.id < b.id; });

    return versions;
}

void SaveVersionList(std::vector<Version> const& versions, std::filesystem::path const& path) {
    std::filesystem::path out_path =
        path.empty() ? Common::FS::GetUserPath(Common::FS::PathType::LauncherDir) / "versions.json"
                     : path;

    json root = json::array();

    for (const auto& v : versions) {
        root.push_back({{"name", v.name},
                        {"path", v.path},
                        {"date", v.date},
                        {"codename", v.codename},
                        {"type", static_cast<int>(v.type)}});
    }

    std::ofstream ofs(out_path, std::ios::trunc);
    if (!ofs) {
        fmt::print(stderr, "Failed to open file for writing: {}\n", out_path.string());
        return;
    }

    ofs << std::setw(4) << root;
}

void AddNewVersion(Version const& v, std::filesystem::path const& path) {
    auto versions = GetVersionList(path);
    versions.push_back(v);
    SaveVersionList(versions, path);
}

void RemoveVersion(std::string const& v_name, std::filesystem::path const& path) {
    auto versions = GetVersionList(path);
    auto it = std::find_if(versions.begin(), versions.end(),
                           [&](const Version& i) { return i.name == v_name; });
    if (it != versions.end()) {
        versions.erase(it);
    }
    SaveVersionList(versions, path);
}

void RemoveVersion(Version const& v, std::filesystem::path const& path) {
    RemoveVersion(v.name, path);
}

} // namespace VersionManager
