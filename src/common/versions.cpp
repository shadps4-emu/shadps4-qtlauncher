// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logging/log.h"
#include "path_util.h"
#include "versions.h"

namespace VersionManager {

std::vector<Version> GetVersionList(std::filesystem::path const& path) {
    toml::ordered_value data;
    try {
        if (path.empty()) {
            data = toml::parse(Common::FS::GetUserPath(Common::FS::PathType::UserDir) /
                               "versions.toml");
        } else {
            data = toml::parse(path);
        }
    } catch (std::exception& ex) {
        fmt::print("Got exception trying to load config file. Exception: {}\n", ex.what());
        return {};
    }

    std::vector<Version> versions;
    for (const auto& [key, value] : data.as_table()) {
        Version v = {
            .name = toml::find_or<std::string>(value, "name", "no name"),
            .path = toml::find_or<std::string>(value, "path", ""),
            .date = toml::find_or<std::string>(value, "date", "never"),
            .codename = toml::find_or<std::string>(value, "codename", ""),
            .type =
                static_cast<VersionType>(toml::find_or<int>(value, "type", VersionType::Custom)),
            .id = std::stoi(key),
        };
        versions.push_back(std::move(v));
    }
    std::sort(versions.begin(), versions.end(), [](Version a, Version b) { return a.id < b.id; });
    return std::move(versions);
}

void SaveVersionList(std::vector<Version> const& versions, std::filesystem::path const& path) {
    toml::ordered_value root = toml::table();

    for (size_t i = 0; i < versions.size(); ++i) {
        const auto& v = versions[i];
        toml::ordered_value entry = toml::table{{"name", v.name},
                                                {"path", v.path},
                                                {"date", v.date},
                                                {"codename", v.codename},
                                                {"type", static_cast<int>(v.type)}};
        root[std::to_string(i)] = std::move(entry);
    }

    std::ofstream ofs(path.empty()
                          ? Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "versions.toml"
                          : path);
    ofs << root;
}

void AddNewVersion(Version const& v, std::filesystem::path const& path) {
    auto versions = GetVersionList(path);
    versions.push_back(v);
    SaveVersionList(versions, path);
}

void RemoveVersion(Version const& v, std::filesystem::path const& path) {
    auto versions = GetVersionList(path);
    auto it = std::find_if(versions.cbegin(), versions.cend(),
                           [v](Version i) { return i.name == v.name; });
    if (it != versions.cend()) {
        versions.erase(it);
    }
    SaveVersionList(versions, path);
}

void RemoveVersion(std::string const& v_name, std::filesystem::path const& path) {
    auto versions = GetVersionList(path);
    auto it = std::find_if(versions.cbegin(), versions.cend(),
                           [v_name](Version i) { return i.name == v_name; });
    if (it != versions.cend()) {
        versions.erase(it);
    }
    SaveVersionList(versions, path);
}

} // namespace VersionManager