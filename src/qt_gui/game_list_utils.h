// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <vector>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "common/path_util.h"
#include "compatibility_info.h"
#include "core/emulator_settings.h"
#include "core/file_sys/game_backend.h"

struct GameInfo {
    std::filesystem::path path; // root path of game directory
                                // (normally directory that contains eboot.bin)
    std::filesystem::path update_path;
    std::filesystem::path icon_path; // path of icon0.png
    std::filesystem::path pic_path;  // path of pic1.png
    std::filesystem::path snd0_path; // path of snd0.at9
    QImage icon;
    std::string size;
    u64 stored_size{};
    QString storage_tooltip;
    // variables extracted from param.sfo
    std::string name = "Unknown";
    std::string serial = "Unknown";
    std::string version = "Unknown";
    std::string region = "Unknown";
    std::string fw = "Unknown";
    std::string save_dir = "Unknown";

    std::string play_time = "Unknown";
    CompatibilityEntry compatibility = CompatibilityEntry{CompatibilityStatus::Unknown};
};

class GameListUtils : public QObject {
    Q_OBJECT
public:
    static QString FormatSize(u64 size) {
        static const QStringList suffixes = {tr("B"), tr("KB"), tr("MB"), tr("GB"), tr("TB")};
        int suffixIndex = 0;

        double gameSize = static_cast<double>(size);
        while (gameSize >= 1024 && suffixIndex < suffixes.size() - 1) {
            gameSize /= 1024;
            ++suffixIndex;
        }

        // Format the size with a specified precision
        QString sizeString;
        if (gameSize < 10.0) {
            sizeString = QString::number(gameSize, 'f', 2);
        } else if (gameSize < 100.0) {
            sizeString = QString::number(gameSize, 'f', 1);
        } else {
            sizeString = QString::number(gameSize, 'f', 0);
        }

        return sizeString + " " + suffixes[suffixIndex];
    }

    struct StorageInfo {
        u64 stored_size{};
        u64 content_size{};
        bool contains_archive{};
    };

    struct StorageEntry {
        QString name;
        StorageInfo storage;
    };

    static std::vector<std::filesystem::path> GetGameRoots(const GameInfo& game) {
        std::vector<std::filesystem::path> roots{game.path};
        const auto stem_path = Core::FileSys::StripZArchiveExtension(game.path);
        for (const auto& suffix : {"-UPDATE", "-patch"}) {
            std::filesystem::path overlay = stem_path;
            overlay += suffix;
            std::error_code ec;
            if (std::filesystem::is_directory(overlay, ec) && !ec) {
                roots.push_back(overlay);
            }
            overlay += ".zar";
            if (Core::FileSys::IsZArchiveFile(overlay)) {
                roots.push_back(overlay);
            }
        }
        return roots;
    }

    static QString FormatStorage(const StorageInfo& storage) {
        QString result = FormatSize(storage.stored_size);
        if (storage.contains_archive && storage.content_size > 0) {
            const double saved = 100.0 * (1.0 - static_cast<double>(storage.stored_size) /
                                                    static_cast<double>(storage.content_size));
            result += saved >= 0.0 ? tr(" (-%1%)").arg(QString::number(saved, 'f', 1))
                                   : tr(" (+%1%)").arg(QString::number(-saved, 'f', 1));
        }
        return result;
    }

    static QString GetStorageTooltip(const std::vector<StorageEntry>& entries) {
        QStringList lines;
        StorageInfo total;
        for (const auto& entry : entries) {
            lines << tr("%1: %2").arg(entry.name, FormatStorage(entry.storage));
            total.stored_size += entry.storage.stored_size;
            total.content_size += entry.storage.content_size;
            total.contains_archive |= entry.storage.contains_archive;
        }
        lines << "" << tr("Total: %1").arg(FormatStorage(total));
        return lines.join('\n');
    }

    static StorageEntry MeasureStorageEntry(const std::filesystem::path& root) {
        const bool is_archive = Core::FileSys::IsZArchiveFile(root);
        const u64 stored_size = Core::FileSys::GetGameRootSize(root);
        const u64 content_size =
            is_archive ? Core::FileSys::GetGameRootContentSize(root) : stored_size;
        QString name;
        Common::FS::PathToQString(name, root.filename());
        return {name, {stored_size, content_size, is_archive}};
    }

    static void GetFolderSize(GameInfo& game, bool force_recalculate = false) {
        QDir cacheDir =
            QDir(Common::FS::GetUserPath(Common::FS::PathType::LauncherMetaData) / game.serial);
        if (!cacheDir.exists()) {
            cacheDir.mkpath(".");
        }
        QString game_path;
        Common::FS::PathToQString(game_path, game.path);
        const QString cache_name =
            "size_cache_" +
            QString::fromLatin1(
                QCryptographicHash::hash(game_path.toUtf8(), QCryptographicHash::Sha256).toHex()) +
            ".txt";
        QFile size_cache_file(cacheDir.absoluteFilePath(cache_name));
        // Older launchers read only this per-serial file. The path-keyed cache above keeps
        // separate folder and .zar installs of the same serial from replacing each other.
        QFile legacy_cache_file(cacheDir.absoluteFilePath("size_cache.txt"));
        QFile* read_cache_file = size_cache_file.exists() ? &size_cache_file : &legacy_cache_file;

        // Keep the first line readable by older launchers. Older cache formats lack a complete
        // breakdown, so migrate them by calculating once; complete caches need no size scan.
        if (!force_recalculate && read_cache_file->open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(read_cache_file);
            in.readLine();
            const QString version = in.readLine();
            const QString cached_path = in.readLine();
            const bool valid_header = version == "v4" && cached_path == game_path;
            bool stored_ok = false;
            bool content_ok = false;
            const u64 stored_size = in.readLine().toULongLong(&stored_ok);
            const u64 content_size = in.readLine().toULongLong(&content_ok);
            const QString archive_line = in.readLine();
            bool count_ok = false;
            const uint count = in.readLine().toUInt(&count_ok);
            std::vector<StorageEntry> entries;
            bool valid_entries = valid_header && stored_ok && content_ok &&
                                 (archive_line == "0" || archive_line == "1") && count_ok &&
                                 count > 0 && count <= 4096;
            if (valid_entries) {
                entries.reserve(count);
                for (uint index = 0; index < count; ++index) {
                    const QString name = in.readLine();
                    bool entry_stored_ok = false;
                    bool entry_content_ok = false;
                    const u64 entry_stored = in.readLine().toULongLong(&entry_stored_ok);
                    const u64 entry_content = in.readLine().toULongLong(&entry_content_ok);
                    const QString entry_archive = in.readLine();
                    if (name.isEmpty() || !entry_stored_ok || !entry_content_ok ||
                        (entry_archive != "0" && entry_archive != "1")) {
                        valid_entries = false;
                        break;
                    }
                    entries.push_back({name, {entry_stored, entry_content, entry_archive == "1"}});
                }
            }
            read_cache_file->close();

            if (valid_entries) {
                game.stored_size = stored_size;
                game.size = FormatSize(stored_size).toStdString();
                game.storage_tooltip = GetStorageTooltip(entries);
                return;
            }
        }

        const auto roots = GetGameRoots(game);
        StorageInfo storage;
        std::vector<StorageEntry> entries;
        for (size_t index = 0; index < roots.size(); ++index) {
            const auto& root = roots[index];
            entries.push_back(MeasureStorageEntry(root));
            // Only the selected update contributes to the Size column. Keep every installed
            // update in the tooltip, including alternative folder/archive variants.
            if (index == 0 || root == game.update_path) {
                const auto& entry_storage = entries.back().storage;
                storage.stored_size += entry_storage.stored_size;
                storage.content_size += entry_storage.content_size;
                storage.contains_archive |= entry_storage.contains_archive;
            }
        }

        const auto dlc_dir = EmulatorSettings.GetAddonInstallDir() / game.serial;
        std::error_code ec;
        std::vector<std::filesystem::path> dlc_roots;
        for (std::filesystem::directory_iterator
                 it(dlc_dir, std::filesystem::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec)) {
            std::error_code entry_ec;
            if (it->is_directory(entry_ec) || Core::FileSys::IsZArchiveFile(it->path())) {
                dlc_roots.push_back(it->path());
            }
        }
        std::ranges::sort(dlc_roots);
        for (const auto& root : dlc_roots) {
            entries.push_back(MeasureStorageEntry(root));
        }
        game.stored_size = storage.stored_size;
        game.size = FormatSize(storage.stored_size).toStdString();
        game.storage_tooltip = GetStorageTooltip(entries);

        QString cache_contents;
        QTextStream out(&cache_contents);
        out << FormatSize(storage.stored_size) << "\n"
            << "v4\n"
            << game_path << "\n"
            << storage.stored_size << "\n"
            << storage.content_size << "\n"
            << (storage.contains_archive ? "1" : "0") << "\n"
            << entries.size() << "\n";
        for (const auto& entry : entries) {
            out << entry.name << "\n"
                << entry.storage.stored_size << "\n"
                << entry.storage.content_size << "\n"
                << (entry.storage.contains_archive ? "1" : "0") << "\n";
        }
        out.flush();
        const QByteArray cache_bytes = cache_contents.toUtf8();
        for (const auto& cache_path : {size_cache_file.fileName(), legacy_cache_file.fileName()}) {
            QSaveFile cache_file(cache_path);
            if (cache_file.open(QIODevice::WriteOnly)) {
                cache_file.write(cache_bytes);
                cache_file.commit();
            }
        }
    }

    static QString GetRegion(char region) {
        switch (region) {
        case 'U':
            return "USA";
        case 'E':
            return "Europe";
        case 'J':
            return "Japan";
        case 'H':
            return "Asia";
        case 'I':
            return "World";
        default:
            return "Unknown";
        }
    }

    static QString GetAppType(int type) {
        switch (type) {
        case 0:
            return "Not Specified";
        case 1:
            return "FULL APP";
        case 2:
            return "UPGRADABLE";
        case 3:
            return "DEMO";
        case 4:
            return "FREEMIUM";
        default:
            return "Unknown";
        }
    }

    QImage BlurImage(const QImage& image, const QRect& rect, int radius) {
        int tab[] = {14, 10, 8, 6, 5, 5, 4, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2};
        int alpha = (radius < 1) ? 16 : (radius > 17) ? 1 : tab[radius - 1];

        QImage result = image.convertToFormat(QImage::Format_ARGB32);
        int r1 = rect.top();
        int r2 = rect.bottom();
        int c1 = rect.left();
        int c2 = rect.right();

        int bpl = result.bytesPerLine();
        int rgba[4];
        unsigned char* p;

        int i1 = 0;
        int i2 = 3;

        for (int col = c1; col <= c2; col++) {
            p = result.scanLine(r1) + col * 4;
            for (int i = i1; i <= i2; i++)
                rgba[i] = p[i] << 4;

            p += bpl;
            for (int j = r1; j < r2; j++, p += bpl)
                for (int i = i1; i <= i2; i++)
                    p[i] = (rgba[i] += ((p[i] << 4) - rgba[i]) * alpha / 16) >> 4;
        }

        for (int row = r1; row <= r2; row++) {
            p = result.scanLine(row) + c1 * 4;
            for (int i = i1; i <= i2; i++)
                rgba[i] = p[i] << 4;

            p += 4;
            for (int j = c1; j < c2; j++, p += 4)
                for (int i = i1; i <= i2; i++)
                    p[i] = (rgba[i] += ((p[i] << 4) - rgba[i]) * alpha / 16) >> 4;
        }

        for (int col = c1; col <= c2; col++) {
            p = result.scanLine(r2) + col * 4;
            for (int i = i1; i <= i2; i++)
                rgba[i] = p[i] << 4;

            p -= bpl;
            for (int j = r1; j < r2; j++, p -= bpl)
                for (int i = i1; i <= i2; i++)
                    p[i] = (rgba[i] += ((p[i] << 4) - rgba[i]) * alpha / 16) >> 4;
        }

        for (int row = r1; row <= r2; row++) {
            p = result.scanLine(row) + c2 * 4;
            for (int i = i1; i <= i2; i++)
                rgba[i] = p[i] << 4;

            p -= 4;
            for (int j = c1; j < c2; j++, p -= 4)
                for (int i = i1; i <= i2; i++)
                    p[i] = (rgba[i] += ((p[i] << 4) - rgba[i]) * alpha / 16) >> 4;
        }

        return result;
    }

    // Opacity is a float between 0 and 1
    static QImage ChangeImageOpacity(const QImage& image, const QRect& rect, float opacity) {
        // Convert to ARGB32 format to ensure alpha channel support
        QImage result = image.convertToFormat(QImage::Format_ARGB32);

        // Ensure opacity is between 0 and 1
        opacity = std::clamp(opacity, 0.0f, 1.0f);

        // Convert opacity to integer alpha value (0-255)
        int alpha = static_cast<int>(opacity * 255);

        // Process only the specified rectangle area
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = rect.left(); x <= rect.right(); ++x) {
                // Get current pixel
                QRgb pixel = line[x];
                // Keep RGB values, but modify alpha while preserving relative transparency
                int newAlpha = (qAlpha(pixel) * alpha) / 255;
                line[x] = qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), newAlpha);
            }
        }

        return result;
    }
};
