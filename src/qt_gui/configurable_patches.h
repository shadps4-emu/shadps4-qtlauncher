// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <tuple>
#include <vector>
#include <QString>
#include <QStringList>

namespace CustomPatches {

struct OptionData {
    QString optionName;
    QString optionNotes;
    std::vector<std::tuple<std::string, int>> modifiedValues;
};

struct ConfigPatchInfo {
    QStringList serialList;
    QString patchName;
    QString appVer;
    std::vector<OptionData> optionData;
};

std::vector<ConfigPatchInfo> GetGamePatchInfo(std::filesystem::path patchFile);
std::vector<ConfigPatchInfo> GetAllPatchInfo();

} // namespace CustomPatches
