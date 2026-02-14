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

std::vector<ConfigPatchInfo> GetConfigPatchInfo(std::filesystem::path patchFile);

// Patch information follows

///////////////// Bloodborne

// Bloodborne Resolution Patch

const ConfigPatchData BloodborneResHor = {
    .dataName = "Resolution (Horizontal)",
    .address = "0x055289f8",
    .minValue = 600,
    .maxValue = 10000,
};

const ConfigPatchData BloodborneResVert = {
    .dataName = "Resolution (Vertical)",
    .address = "0x055289fc",
    .minValue = 400,
    .maxValue = 6000,
};

const ConfigPatchInfo BBRes = {
    .patchName = "Resolution Patch (560p)",
    .appVer = "01.09",
    .serialList = {"CUSA00207", "CUSA00208", "CUSA00299", "CUSA00900", "CUSA01363", "CUSA03023",
                   "CUSA03173"},
    .patchData = {BloodborneResHor, BloodborneResVert},
};

// Placeholder Other Bloodborne Patches

///////////////// Placeholder Other Games

// Placeholder Other Games Patch

///////////////// All patch info for iterator
const std::vector<ConfigPatchInfo> AllPatchInfo = {BBRes};

} // namespace CustomPatches
