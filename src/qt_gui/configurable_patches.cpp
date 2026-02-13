// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFile>
#include <QXmlStreamReader>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "configurable_patches.h"

using namespace CustomPatches;

std::vector<ConfigPatchInfo> CustomPatches::GetConfigPatchInfo(std::filesystem::path patchFile) {
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
