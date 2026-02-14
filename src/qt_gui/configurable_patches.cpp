// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <QFile>
#include <QMessageBox>
#include <QXmlStreamReader>
#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "configurable_patches.h"

using json = nlohmann::json;
using namespace CustomPatches;

std::vector<ConfigPatchInfo> CustomPatches::GetGamePatchInfo(std::filesystem::path patchFile) {
    std::vector<ConfigPatchInfo> patchInfo;

    QString filePath;
    Common::FS::PathToQString(filePath, patchFile);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR(Loader, "Unable to open the file for reading.");
        return {};
    }

    const QByteArray xmlData = file.readAll();
    file.close();

    QXmlStreamReader xmlReader(xmlData);
    std::string currentPatchName;
    bool serialMatch = false;

    std::vector<ConfigPatchInfo> AllPatchInfo = GetAllPatchInfo();
    for (const ConfigPatchInfo& currentInfo : AllPatchInfo) {

        while (!xmlReader.atEnd()) {
            xmlReader.readNext();
            if (!xmlReader.isStartElement()) {
                continue;
            }

            if (xmlReader.name() == QStringLiteral("Metadata")) {
                const QString appVer = xmlReader.attributes().value("AppVer").toString();
                QString name = xmlReader.attributes().value("Name").toString();
                currentPatchName = name.toStdString();

                if (currentInfo.appVer != appVer || currentPatchName != currentInfo.patchName)
                    continue;

                if (serialMatch) {
                    patchInfo.push_back(currentInfo);
                    serialMatch = false;
                }
            } else if (xmlReader.name() == QStringLiteral("TitleID")) {
                while (xmlReader.readNextStartElement()) {
                    QString serial = xmlReader.readElementText();
                    if (currentInfo.serialList.contains(serial)) {
                        serialMatch = true;
                        break;
                    }
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
