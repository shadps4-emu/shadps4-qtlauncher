// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <vector>
#include <QString>
#include <QStringList>

namespace CustomPatches {

struct ConfigPatchData {
    QString dataName;
    QString address;
    int minValue;
    int maxValue;
};

struct ConfigPatchInfo {
    QString patchName;
    QString appVer;
    QStringList serialList;
    std::vector<ConfigPatchData> patchData;
};

std::vector<ConfigPatchInfo> GetGamePatchInfo(std::filesystem::path patchFile);
std::vector<ConfigPatchInfo> GetAllPatchInfo();

} // namespace CustomPatches
