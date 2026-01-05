// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <codecvt>
#include <sstream>
#include <string>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QString>
#include <QXmlStreamReader>
#include <pugixml.hpp>
#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/file_format/psf.h"
#include "memory_patcher.h"

namespace MemoryPatcher {

std::string toHex(u64 value, size_t byteSize) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(byteSize * 2) << value;
    return ss.str();
}

std::string convertValueToHex(const std::string type, const std::string valueStr) {
    std::string result;

    if (type == "byte") {
        const u32 value = std::stoul(valueStr, nullptr, 16);
        result = toHex(value, 1);
    } else if (type == "bytes16") {
        const u32 value = std::stoul(valueStr, nullptr, 16);
        result = toHex(value, 2);
    } else if (type == "bytes32") {
        const u32 value = std::stoul(valueStr, nullptr, 16);
        result = toHex(value, 4);
    } else if (type == "bytes64") {
        const u64 value = std::stoull(valueStr, nullptr, 16);
        result = toHex(value, 8);
    } else if (type == "float32") {
        union {
            float f;
            uint32_t i;
        } floatUnion;
        floatUnion.f = std::stof(valueStr);
        result = toHex(std::byteswap(floatUnion.i), sizeof(floatUnion.i));
    } else if (type == "float64") {
        union {
            double d;
            uint64_t i;
        } doubleUnion;
        doubleUnion.d = std::stod(valueStr);
        result = toHex(std::byteswap(doubleUnion.i), sizeof(doubleUnion.i));
    } else if (type == "utf8") {
        std::vector<unsigned char> byteArray =
            std::vector<unsigned char>(valueStr.begin(), valueStr.end());
        byteArray.push_back('\0');
        std::stringstream ss;
        for (unsigned char c : byteArray) {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(c);
        }
        result = ss.str();
    } else if (type == "utf16") {
        std::wstring wide_str(valueStr.size(), L'\0');
        std::mbstowcs(&wide_str[0], valueStr.c_str(), valueStr.size());
        wide_str.resize(std::wcslen(wide_str.c_str()));

        std::u16string valueStringU16;

        for (wchar_t wc : wide_str) {
            if (wc <= 0xFFFF) {
                valueStringU16.push_back(static_cast<char16_t>(wc));
            } else {
                wc -= 0x10000;
                valueStringU16.push_back(static_cast<char16_t>(0xD800 | (wc >> 10)));
                valueStringU16.push_back(static_cast<char16_t>(0xDC00 | (wc & 0x3FF)));
            }
        }

        std::vector<unsigned char> byteArray;
        // convert to little endian
        for (char16_t ch : valueStringU16) {
            unsigned char low_byte = static_cast<unsigned char>(ch & 0x00FF);
            unsigned char high_byte = static_cast<unsigned char>((ch >> 8) & 0x00FF);

            byteArray.push_back(low_byte);
            byteArray.push_back(high_byte);
        }
        byteArray.push_back('\0');
        byteArray.push_back('\0');
        std::stringstream ss;

        for (unsigned char ch : byteArray) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
        result = ss.str();
    } else if (type == "bytes") {
        result = valueStr;
    } else if (type == "mask" || type == "mask_jump32") {
        result = valueStr;
    } else {
        LOG_INFO(Loader, "Error applying Patch, unknown type: {}", type);
    }
    return result;
}

std::vector<PendingPatch> readPatches(std::string gameSerial, std::string appVersion) {
    std::vector<PendingPatch> pending;

    QString patchDir;
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
        const QJsonObject jsonObject = jsonDoc.object();

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

            if (!xmlReader.isStartElement()) {
                continue;
            }

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
            } else if (xmlReader.name() == QStringLiteral("PatchList")) {
                while (!xmlReader.atEnd() &&
                       !(xmlReader.tokenType() == QXmlStreamReader::EndElement &&
                         xmlReader.name() == QStringLiteral("PatchList"))) {

                    xmlReader.readNext();

                    if (xmlReader.tokenType() != QXmlStreamReader::StartElement ||
                        xmlReader.name() != QStringLiteral("Line")) {
                        continue;
                    }

                    const QXmlStreamAttributes a = xmlReader.attributes();
                    const QString type = a.value("Type").toString();
                    const QString addr = a.value("Address").toString();
                    QString val = a.value("Value").toString();
                    const QString offStr = a.value("Offset").toString();
                    const QString tgt =
                        (type == "mask_jump32") ? a.value("Target").toString() : QString{};
                    const QString sz =
                        (type == "mask_jump32") ? a.value("Size").toString() : QString{};

                    if (!isEnabled) {
                        continue;
                    }
                    if ((type != "mask" && type != "mask_jump32") && !versionMatches) {
                        continue;
                    }

                    PendingPatch pp;
                    pp.modName = currentPatchName;
                    pp.address = addr.toStdString();

                    if (type == "mask" || type == "mask_jump32") {
                        if (!offStr.toStdString().empty()) {
                            pp.maskOffset = std::stoi(offStr.toStdString(), nullptr, 10);
                        }
                        pp.mask = (type == "mask") ? MemoryPatcher::PatchMask::Mask
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

} // namespace MemoryPatcher
