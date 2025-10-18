// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <toml.hpp>
#include "types.h"

namespace VersionManager {

enum VersionType {
    Release = 0,
    Nightly = 1,
    Custom = 2,
};

struct Version {
    std::string name;
    std::string path;
    std::string date;
    std::string codename;
    VersionType type;
    s32 id;
};

std::vector<Version> GetVersionList(std::filesystem::path const& path = "");

void AddNewVersion(Version const& v, std::filesystem::path const& path = "");
void RemoveVersion(Version const& v, std::filesystem::path const& path = "");
void RemoveVersion(std::string const& v, std::filesystem::path const& path = "");

} // namespace VersionManager