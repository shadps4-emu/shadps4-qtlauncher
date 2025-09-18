// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QMessageBox>
#include <QString>
#include <QXmlStreamReader>

#include "common/path_util.h"
#include "common/logging/log.h"
#include "qt_gui/game_info.h"

#include "memory_patcher.h"

namespace MemoryPatcher {
QString patchDir;

std::vector<PendingPatch> readPatches(std::string gameSerial, std::string appVersion) {
    std::vector<PendingPatch> pending;

    Common::FS::PathToQString(patchDir, Common::FS::GetUserPath(Common::FS::PathType::PatchesDir));
    QDir dir(patchDir);
    QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& folder : folders) {
        QString filesJsonPath = patchDir + "/" + folder + "/files.json";

        QFile jsonFile(filesJsonPath);
        if (!jsonFile.open(QIODevice::ReadOnly)) {
            LOG_ERROR(Loader, "Unable to open files.json for reading in repository {}",
                      folder.toStdString());
            continue;
        }
        const QByteArray jsonData = jsonFile.readAll();
        jsonFile.close();

        const QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
        const QJsonObject   jsonObject = jsonDoc.object();

        QString selectedFileName;
        const QString serial = QString::fromStdString(gameSerial);

        for (auto it = jsonObject.constBegin(); it != jsonObject.constEnd(); ++it) {
            const QString filePath = it.key();
            const QJsonArray idsArray = it.value().toArray();
            if (idsArray.contains(QJsonValue(serial))) {
                selectedFileName = filePath;
                break;
            }
        }

        if (selectedFileName.isEmpty()) {
            LOG_ERROR(Loader, "No patch file found for the current serial in repository {}",
                      folder.toStdString());
            continue;
        }

        const QString filePath = patchDir + "/" + folder + "/" + selectedFileName;
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            LOG_ERROR(Loader, "Unable to open the file for reading.");
            continue;
        }
        const QByteArray xmlData = file.readAll();
        file.close();

        QXmlStreamReader xmlReader(xmlData);

        bool isEnabled = false;
        std::string currentPatchName;

        bool versionMatches = true;

        while (!xmlReader.atEnd()) {
            xmlReader.readNext();

            if (!xmlReader.isStartElement()) continue;

            if (xmlReader.name() == QStringLiteral("Metadata")) {
                QString name = xmlReader.attributes().value("Name").toString();
                currentPatchName = name.toStdString();

                const QString appVer = xmlReader.attributes().value("AppVer").toString();

                isEnabled = false;
                for (const QXmlStreamAttribute& attr : xmlReader.attributes()) {
                    if (attr.name() == QStringLiteral("isEnabled")) {
                        isEnabled = (attr.value().toString() == "true");
                    }
                }
                versionMatches = (appVer.toStdString() == appVersion);
            }
            else if (xmlReader.name() == QStringLiteral("PatchList")) {
                while (!xmlReader.atEnd() &&
                      !(xmlReader.tokenType() == QXmlStreamReader::EndElement &&
                        xmlReader.name() == QStringLiteral("PatchList"))) {

                    xmlReader.readNext();

                    if (xmlReader.tokenType() != QXmlStreamReader::StartElement ||
                        xmlReader.name() != QStringLiteral("Line")) {
                        continue;
                    }

                    const QXmlStreamAttributes a = xmlReader.attributes();
                    const QString type   = a.value("Type").toString();
                    const QString addr   = a.value("Address").toString();
                    QString       val    = a.value("Value").toString();
                    const QString offStr = a.value("Offset").toString();
                    const QString tgt    = (type == "mask_jump32") ? a.value("Target").toString() : QString{};
                    const QString sz     = (type == "mask_jump32") ? a.value("Size").toString()   : QString{};

                    if (!isEnabled) continue;
                    if ((type != "mask" && type != "mask_jump32") && !versionMatches) continue;

                    PendingPatch pp;
                    pp.modName  = currentPatchName;
                    pp.address  = addr.toStdString();

                    if (type == "mask" || type == "mask_jump32") {
                        if (!offStr.toStdString().empty()) {
                            pp.maskOffset = std::stoi(offStr.toStdString(), nullptr, 10);
                        }
                        pp.mask = (type == "mask")
                                 ? MemoryPatcher::PatchMask::Mask
                                 : MemoryPatcher::PatchMask::Mask_Jump32;
                        pp.value = val.toStdString();
                        pp.target = tgt.toStdString();
                        pp.size = sz.toStdString();
                    } else {
                        pp.value = convertValueToHex(type.toStdString(), val.toStdString());
                        pp.target.clear();
                        pp.size.clear();
                        pp.mask = MemoryPatcher::PatchMask::None;
                        pp.maskOffset = 0;
                    }

                    pp.littleEndian = (type == "bytes16" || type == "bytes32" || type == "bytes64");
                    pending.emplace_back(std::move(pp));
                }
            }
        }

        if (xmlReader.hasError()) {
            LOG_ERROR(Loader, "Failed to parse XML for {}", gameSerial);
        } else {
            LOG_INFO(Loader, "Patches parsed successfully, repository: {}", folder.toStdString());
        }


    }
    return pending;
}

}