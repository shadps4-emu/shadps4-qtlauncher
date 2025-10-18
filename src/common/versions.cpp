// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "logging/log.h"
#include "versions.h"
#include "path_util.h"

namespace VersionManager {

std::vector<Version> GetVersionList(std::filesystem::path const& path) {
    toml::ordered_value data;
    try {
        if (path.empty()) {
            data = toml::parse(Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "versions.toml");
        } else {
            data = toml::parse(path);
        }
    } catch (std::exception& ex) {
        fmt::print("Got exception trying to load config file. Exception: {}\n", ex.what());
        return {};
    }

    std::vector<Version> versions;
    for (const auto& [key, value] : data.as_table()) {
        Version v;
        v.name = toml::find_or<std::string>(value, "name", "no name");
        v.path = toml::find_or<std::string>(value, "path", "");
        v.date = toml::find_or<std::string>(value, "date", "never");
        v.codename = toml::find_or<std::string>(value, "codename", "");
        v.type = static_cast<VersionType>(toml::find_or<int>(value, "type", VersionType::Custom));
        versions.push_back(std::move(v));
    }

    return std::move(versions);
}

} // namespace VersionManager