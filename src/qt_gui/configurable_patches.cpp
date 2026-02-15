// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <QFile>
#include <QMessageBox>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "configurable_patches.h"

using json = nlohmann::json;
using namespace CustomPatches;

std::vector<ConfigPatchInfo> CustomPatches::GetGamePatchInfo(std::filesystem::path patchFile) {
    std::vector<ConfigPatchInfo> patchInfo;
    bool serialMatch = false;

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(patchFile.c_str());

    if (!result) {
        LOG_ERROR(Loader, "Unable to open file {} for reading", patchFile.string());
    }

    auto patchDoc = doc.child("Patch");

    std::vector<ConfigPatchInfo> AllPatchInfo = GetAllPatchInfo();
    for (const ConfigPatchInfo& currentInfo : AllPatchInfo) {
        for (pugi::xml_node& node : patchDoc.children()) {
            if (std::string_view(node.name()) == "TitleID") {
                for (pugi::xml_node item = node.child("ID"); item; item = item.next_sibling("ID")) {
                    std::string serial = item.text().get();
                    if (currentInfo.serialList.contains(serial)) {
                        serialMatch = true;
                    }
                }
            }

            if (std::string_view(node.name()) == "Metadata") {
                std::string appVer = node.attribute("AppVer").as_string();
                std::string name = node.attribute("Name").as_string();

                if (currentInfo.appVer != appVer || name != currentInfo.patchName)
                    continue;

                if (serialMatch) {
                    patchInfo.push_back(currentInfo);
                }
            }
        }
    }

    return patchInfo;
}

std::vector<ConfigPatchInfo> CustomPatches::GetAllPatchInfo() {
    std::vector<ConfigPatchInfo> allPatchInfo;
    std::filesystem::path filePath = Common::FS::GetUserPath(Common::FS::PathType::PatchesDir) /
                                     "shadPS4" / "configurable_patches.json";
    std::ifstream file(filePath.c_str());
    json data = json::parse(file);

    for (auto& [key, value] : data.items()) {
        std::vector<std::string> serialVector;
        std::string patchName;
        std::string appVer;
        std::vector<json> patchValues;
        ConfigPatchInfo currentPatch;

        if (value.is_object()) {
            for (auto& [objkey, objvalue] : value.items()) {
                if (objkey == "Name") {
                    patchName = objvalue.get<std::string>();
                } else if (objkey == "AppVer") {
                    appVer = objvalue.get<std::string>();
                } else if (objkey == "Serial") {
                    serialVector = objvalue.get<std::vector<std::string>>();
                } else if (objkey == "PatchValues") {
                    patchValues = objvalue.get<std::vector<json>>();
                }
            }

            currentPatch.patchName = QString::fromStdString(patchName);
            currentPatch.appVer = QString::fromStdString(appVer);

            for (const std::string& serial : serialVector) {
                currentPatch.serialList.append(QString::fromStdString(serial));
            }

            for (const json& value : patchValues) {
                ConfigPatchData patchdata = {
                    .dataName = QString::fromStdString(value["ValueName"].get<std::string>()),
                    .address = QString::fromStdString(value["Address"].get<std::string>()),
                    .minValue = value["Min"].get<int>(),
                    .maxValue = value["Max"].get<int>(),
                };
                currentPatch.patchData.push_back(patchdata);
            }
            allPatchInfo.push_back(currentPatch);
        }
    }

    return allPatchInfo;
}
