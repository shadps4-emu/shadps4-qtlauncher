// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <QCoreApplication>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QXmlStreamReader>
#include <common/path_util.h>
#include <common/scm_rev.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/versions.h"
#include "emulator_settings.h"
#include "qt_gui/gui_settings.h"
#include "qt_gui/main_window.h"
#include "user_settings.h"

using json = nlohmann::json;

namespace fs = std::filesystem;

#define tr(...) MainWindow::tr(__VA_ARGS__)

enum class TransferOption : s32 {
    Copy = 0,
    Move,
    MoveAndLinkBack,
    Nothing,
    SdlCancelled = -1,
};

static void MovePath(fs::path const& _from, fs::path const& _to) {
    try {
        fs::rename(_from, _to);
    } catch (...) {
        fs::copy(_from, _to, fs::copy_options::recursive | fs::copy_options::skip_existing);
    }
}

static void CheckAndMigrateTrophies(TransferOption option) {
    LOG_INFO(Loader, "Starting trophy migration");
    const auto user_dir = EmulatorSettings.GetHomeDir() / FormatUidHex(1000);
    const auto old_user_dir = EmulatorSettings.GetHomeDir() / "1000";
    const auto old_trophy_base_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "game_data";
    const auto new_trophy_global_dir = Common::FS::GetUserPath(Common::FS::PathType::TrophyDir);
    if (!fs::exists(old_trophy_base_dir)) {
        return;
    }

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(old_trophy_base_dir, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const auto trophy_files_dir = entry.path() / "TrophyFiles";
        if (!fs::exists(trophy_files_dir, ec)) {
            continue;
        }

        for (const auto& subentry : fs::directory_iterator(trophy_files_dir, ec)) {
            if (!subentry.is_directory(ec)) {
                continue;
            }

            const auto old_trophy_dir = subentry.path();
            const auto xml_path = old_trophy_dir / "Xml" / "TROP.XML";
            if (!fs::exists(xml_path, ec)) {
                continue;
            }

            QFile file(QString::fromStdString(xml_path.string()));
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }

            QXmlStreamReader xml(&file);
            std::string npcommid;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == u"npcommid") {
                    npcommid = xml.readElementText().toStdString();
                    break;
                }
            }

            if (xml.hasError() || npcommid.empty()) {
                continue;
            }

            const auto new_trophy_file = user_dir / "trophy" / (npcommid + ".xml");
            const auto old_dest_trophy_file = old_user_dir / "trophy" / (npcommid + ".xml");
            if (fs::exists(new_trophy_file, ec) || fs::exists(old_dest_trophy_file, ec)) {
                continue;
            }

            const auto new_trophy_dir = new_trophy_global_dir / npcommid;
            if (!fs::exists(new_trophy_dir, ec)) {
                fs::create_directories(new_trophy_dir, ec);
                fs::copy(old_trophy_dir, new_trophy_dir, fs::copy_options::recursive, ec);
            }

            fs::create_directories(user_dir / "trophy", ec);
            switch (option) {
            case TransferOption::Copy:
                fs::copy_file(xml_path, new_trophy_file, ec);
                break;
            case TransferOption::Move:
                MovePath(xml_path, new_trophy_file);
                break;
            case TransferOption::MoveAndLinkBack:
                MovePath(xml_path, new_trophy_file);
                fs::create_symlink(new_trophy_file, xml_path, ec);
                break;
            default:
                break;
            }
        }
    }
    LOG_INFO(Loader, "Trophy migration complete");
}

static TransferOption AskSaveDataMigrationOption(QWidget* parent = nullptr) {
    QMessageBox msgbox(parent);

    msgbox.setWindowTitle(tr("Save Data Migration"));
#ifdef _WIN32
    msgbox.setText(
        tr("The shadPS4 save data location has been updated, and existing save files have been "
           "detected in the old location.\n\n"
           "After migration, rebuilding the save database will be required for the emulator to "
           "detect your saves. You will be prompted to rebuild it, or you can run 'Rebuild Save "
           "Database' from the File menu at any time.\n\n"
           "Do you wish to copy them over, move them over, or continue without doing anything?"));
#else
    msgbox.setText(
        tr("The shadPS4 save data location has been updated, and existing save files have been "
           "detected in the old location.\n\n"
           "After migration, rebuilding the save database will be required for the emulator to "
           "detect your saves. You will be prompted to rebuild it, or you can run 'Rebuild Save "
           "Database' from the File menu at any time.\n\n"
           "Do you wish to copy them over, move them over, move and link back to the original "
           "location, or continue without doing anything?"));
#endif

    auto* copy_btn = (QAbstractButton*)msgbox.addButton(tr("Copy"), QMessageBox::AcceptRole);
    auto* move_btn = (QAbstractButton*)msgbox.addButton(tr("Move"), QMessageBox::AcceptRole);

#ifndef _WIN32
    auto* link_btn =
        (QAbstractButton*)msgbox.addButton(tr("Move and link back"), QMessageBox::AcceptRole);
#endif

    auto* nothing_btn =
        (QAbstractButton*)msgbox.addButton(tr("Do nothing"), QMessageBox::RejectRole);

    msgbox.exec();

    auto* clicked = msgbox.clickedButton();

    if (clicked == copy_btn)
        return TransferOption::Copy;

    if (clicked == move_btn)
        return TransferOption::Move;

#ifndef _WIN32
    if (clicked == link_btn)
        return TransferOption::MoveAndLinkBack;
#endif

    return TransferOption::Nothing;
}

bool HasUnmigratedSaveData() {
    const auto home_dir = EmulatorSettings.GetHomeDir();
    const auto migration_done_path = home_dir / ".savedata_migration.complete";
    std::error_code ec;

    if (fs::exists(migration_done_path, ec)) {
        return false;
    }

    // 1. Dinosaur path: UserDir / "savedata" / "1"
    const auto old_dino_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "savedata" / "1";
    if (fs::exists(old_dino_dir, ec) && !fs::is_symlink(old_dino_dir, ec) &&
        !fs::is_empty(old_dino_dir, ec)) {
        return true;
    }

    // 2. Decimal paths for registered users
    for (const auto& user : UserSettings.GetUserManager().GetAllUsers()) {
        if (user.user_id < 0) {
            continue;
        }
        const auto old_save_dir = home_dir / std::to_string(user.user_id) / "savedata";
        const auto new_save_dir = home_dir / FormatUidHex(user.user_id) / "savedata";
        if (old_save_dir != new_save_dir && fs::exists(old_save_dir, ec) &&
            !fs::is_symlink(old_save_dir, ec) && !fs::is_empty(old_save_dir, ec)) {
            return true;
        }
    }

    // 3. Decimal directories in home_dir
    if (fs::exists(home_dir, ec)) {
        for (const auto& entry : fs::directory_iterator(home_dir, ec)) {
            if (!entry.is_directory(ec) || entry.is_symlink(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (!name.empty() && std::all_of(name.begin(), name.end(), ::isdigit)) {
                try {
                    const u64 raw_id = std::stoull(name);
                    if (raw_id <= 0xFFFFFFFFULL) {
                        const auto target_name = FormatUidHex(static_cast<u32>(raw_id));
                        if (target_name != name) {
                            const auto old_save_dir = entry.path() / "savedata";
                            if (fs::exists(old_save_dir, ec) && !fs::is_symlink(old_save_dir, ec) &&
                                !fs::is_empty(old_save_dir, ec)) {
                                return true;
                            }
                        }
                    }
                } catch (...) {
                }
            }
        }
    }

    // 4. Old trophy base dir
    const auto old_trophy_base_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "game_data";
    if (fs::exists(old_trophy_base_dir, ec) && !fs::is_empty(old_trophy_base_dir, ec)) {
        for (const auto& entry : fs::directory_iterator(old_trophy_base_dir, ec)) {
            if (entry.is_directory(ec) && fs::exists(entry.path() / "TrophyFiles", ec)) {
                return true;
            }
        }
    }

    return false;
}

static bool g_rebuild_prompted = false;

static fs::path FindEmulatorExecutable() {
    gui_settings settings{};
    const QString selected = settings.GetValue(gui::vm_versionSelected).toString();
    if (!selected.isEmpty() && fs::exists(selected.toStdString())) {
        return selected.toStdString();
    }
    const auto versions = VersionManager::GetVersionList();
    for (const auto& v : versions) {
        if (!v.path.empty() && fs::exists(v.path)) {
            return v.path;
        }
    }
    const auto app_dir = QCoreApplication::applicationDirPath().toStdString();
#ifdef _WIN32
    const auto local_exe = fs::path(app_dir) / "shadps4.exe";
#else
    const auto local_exe = fs::path(app_dir) / "shadps4";
#endif
    if (fs::exists(local_exe)) {
        return local_exe;
    }
    return {};
}

bool PerformSaveDataMigration(QWidget* parent) {
    LOG_INFO(Loader, "Starting unified save data migration");
    const auto home_dir = EmulatorSettings.GetHomeDir();
    const auto migration_done_path = home_dir / ".savedata_migration.complete";
    std::error_code ec;

    TransferOption user_choice = AskSaveDataMigrationOption(parent);
    if (user_choice == TransferOption::Nothing || user_choice == TransferOption::SdlCancelled) {
        LOG_INFO(Loader, "Save data migration cancelled or deferred by user");
        return false;
    }

    std::vector<std::pair<fs::path, fs::path>> migration_pairs;

    // 1. Dinosaur path: UserDir / "savedata" / "1" -> home_dir / "000003e8" / "savedata"
    const auto old_dino_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "savedata" / "1";
    const auto dino_target = home_dir / FormatUidHex(1000) / "savedata";
    if (fs::exists(old_dino_dir, ec) && !fs::is_symlink(old_dino_dir, ec) &&
        !fs::is_empty(old_dino_dir, ec)) {
        migration_pairs.emplace_back(old_dino_dir, dino_target);
    }

    // 2. Decimal paths for registered users
    for (const auto& user : UserSettings.GetUserManager().GetAllUsers()) {
        if (user.user_id < 0) {
            continue;
        }
        const auto old_save_dir = home_dir / std::to_string(user.user_id) / "savedata";
        const auto new_save_dir = home_dir / FormatUidHex(user.user_id) / "savedata";
        if (old_save_dir != new_save_dir && fs::exists(old_save_dir, ec) &&
            !fs::is_symlink(old_save_dir, ec) && !fs::is_empty(old_save_dir, ec)) {
            if (std::find_if(migration_pairs.begin(), migration_pairs.end(), [&](const auto& p) {
                    return p.first == old_save_dir;
                }) == migration_pairs.end()) {
                migration_pairs.emplace_back(old_save_dir, new_save_dir);
            }
        }
    }

    // 3. Decimal paths found in directory scan
    if (fs::exists(home_dir, ec)) {
        for (const auto& entry : fs::directory_iterator(home_dir, ec)) {
            if (!entry.is_directory(ec) || entry.is_symlink(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (!name.empty() && std::all_of(name.begin(), name.end(), ::isdigit)) {
                try {
                    const u64 raw_id = std::stoull(name);
                    if (raw_id <= 0xFFFFFFFFULL) {
                        const auto target_name = FormatUidHex(static_cast<u32>(raw_id));
                        if (target_name != name) {
                            const auto old_save_dir = entry.path() / "savedata";
                            const auto new_save_dir = home_dir / target_name / "savedata";
                            if (fs::exists(old_save_dir, ec) && !fs::is_symlink(old_save_dir, ec) &&
                                !fs::is_empty(old_save_dir, ec)) {
                                if (std::find_if(migration_pairs.begin(), migration_pairs.end(),
                                                 [&](const auto& p) {
                                                     return p.first == old_save_dir;
                                                 }) == migration_pairs.end()) {
                                    migration_pairs.emplace_back(old_save_dir, new_save_dir);
                                }
                            }
                        }
                    }
                } catch (...) {
                }
            }
        }
    }

    // Execute save migration for all pairs
    for (const auto& [source_root, target_root] : migration_pairs) {
        LOG_INFO(Loader, "Migrating save data from {} to {}", source_root.string(),
                 target_root.string());
        fs::create_directories(target_root, ec);

        for (const auto& title_entry : fs::directory_iterator(source_root, ec)) {
            if (!title_entry.is_directory(ec)) {
                continue;
            }
            const auto old_title_dir = title_entry.path();
            const auto new_title_dir = target_root / old_title_dir.filename();

            if (!fs::exists(new_title_dir, ec)) {
                switch (user_choice) {
                case TransferOption::Copy:
                    fs::copy(old_title_dir, new_title_dir, fs::copy_options::recursive, ec);
                    break;
                case TransferOption::Move:
                    MovePath(old_title_dir, new_title_dir);
                    break;
                case TransferOption::MoveAndLinkBack:
                    MovePath(old_title_dir, new_title_dir);
                    fs::create_directory_symlink(new_title_dir, old_title_dir, ec);
                    break;
                default:
                    break;
                }
            } else {
                for (const auto& item_entry : fs::directory_iterator(old_title_dir, ec)) {
                    const auto old_item = item_entry.path();
                    const auto new_item = new_title_dir / old_item.filename();

                    if (!fs::exists(new_item, ec)) {
                        switch (user_choice) {
                        case TransferOption::Copy:
                            if (item_entry.is_directory(ec)) {
                                fs::copy(old_item, new_item, fs::copy_options::recursive, ec);
                            } else {
                                fs::copy_file(old_item, new_item, ec);
                            }
                            break;
                        case TransferOption::Move:
                            MovePath(old_item, new_item);
                            break;
                        case TransferOption::MoveAndLinkBack:
                            MovePath(old_item, new_item);
                            if (item_entry.is_directory(ec)) {
                                fs::create_directory_symlink(new_item, old_item, ec);
                            } else {
                                fs::create_symlink(new_item, old_item, ec);
                            }
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
        }
    }

    // Migrate trophies if any exist in old location
    CheckAndMigrateTrophies(user_choice);

    // Mark migration complete only when user actually transferred files
    std::ofstream ofs(migration_done_path);
    ofs << "";

    g_rebuild_prompted = true;
    QMessageBox rebuild_box(parent);
    rebuild_box.setWindowTitle(tr("Rebuild Save Database"));
    rebuild_box.setText(tr("Save data migration completed.\n"
                           "Do you want to rebuild the save database now?"));
    rebuild_box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    rebuild_box.setDefaultButton(QMessageBox::Yes);
    if (rebuild_box.exec() == QMessageBox::Yes) {
        RebuildSaveDataDatabase(parent);
    }

    return true;
}

bool RebuildSaveDataDatabase(QWidget* parent, std::optional<u32> user_id) {
    if (HasUnmigratedSaveData()) {
        QMessageBox warn(parent);
        warn.setWindowTitle(tr("Rebuild Save Database"));
        warn.setIcon(QMessageBox::Warning);
        warn.setText(tr("Unmigrated save files were detected.\n"
                        "Migration must be performed before the save database can be rebuilt.\n\n"
                        "Do you want to migrate your save data now?"));
        warn.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        warn.setDefaultButton(QMessageBox::Yes);
        if (warn.exec() == QMessageBox::Yes) {
            if (!PerformSaveDataMigration(parent)) {
                return false;
            }
        } else {
            return false;
        }
    }

    const auto emu_path = FindEmulatorExecutable();
    if (emu_path.empty()) {
        QMessageBox::warning(parent, tr("Rebuild Save Database"),
                             tr("Emulator executable not found. Please select an emulator version "
                                "in the Version Manager."));
        return false;
    }

    QStringList args{"--rebuild-savedata"};
    if (user_id.has_value()) {
        args.append(QString::fromStdString(FormatUidHex(*user_id)));
    }

    QProcess process;
    process.start(QString::fromStdString(emu_path.string()), args);
    if (!process.waitForStarted(5000)) {
        QMessageBox::critical(parent, tr("Rebuild Save Database"),
                              tr("Failed to start emulator process."));
        return false;
    }

    process.waitForFinished(60000);
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        QMessageBox::information(parent, tr("Rebuild Save Database"),
                                 tr("Save data database successfully rebuilt."));
        return true;
    } else {
        QMessageBox::critical(parent, tr("Rebuild Save Database"),
                              tr("Failed to rebuild save data database."));
        return false;
    }
}

static void CheckMissingSaveDataDatabase(QWidget* parent = nullptr) {
    if (g_rebuild_prompted || HasUnmigratedSaveData()) {
        return;
    }

    const auto home_dir = EmulatorSettings.GetHomeDir();
    const auto user_dir_base = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    std::error_code ec;

    if (!fs::exists(home_dir, ec)) {
        return;
    }

    bool missing_db = false;

    for (const auto& user : UserSettings.GetUserManager().GetAllUsers()) {
        if (user.user_id < 0) {
            continue;
        }
        const auto hex_id = FormatUidHex(user.user_id);
        const auto save_dir = home_dir / hex_id / "savedata";
        if (fs::exists(save_dir, ec) && !fs::is_empty(save_dir, ec)) {
            const auto db_path =
                user_dir_base / "system_data" / "savedata" / hex_id / "db" / "user" / "savedata.db";
            if (!fs::exists(db_path, ec)) {
                missing_db = true;
                break;
            }
        }
    }

    if (!missing_db) {
        for (const auto& entry : fs::directory_iterator(home_dir, ec)) {
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name.size() == 8 &&
                name.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
                const auto save_dir = entry.path() / "savedata";
                if (fs::exists(save_dir, ec) && !fs::is_empty(save_dir, ec)) {
                    const auto db_path = user_dir_base / "system_data" / "savedata" / name / "db" /
                                         "user" / "savedata.db";
                    if (!fs::exists(db_path, ec)) {
                        missing_db = true;
                        break;
                    }
                }
            }
        }
    }

    if (!missing_db) {
        return;
    }

    g_rebuild_prompted = true;
    QMessageBox msgbox(parent);
    msgbox.setWindowTitle(tr("Save Data Database"));
    msgbox.setText(
        tr("The save data database is missing for existing saves.\n"
           "Rebuilding the database is required for the emulator to detect your save files.\n\n"
           "Do you want to rebuild the save database now?"));
    msgbox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgbox.setDefaultButton(QMessageBox::Yes);

    if (msgbox.exec() == QMessageBox::Yes) {
        RebuildSaveDataDatabase(parent);
    }
}

void CheckSaveAndTrophyMigration() {
    try {
        if (HasUnmigratedSaveData()) {
            PerformSaveDataMigration(nullptr);
        } else {
            CheckMissingSaveDataDatabase(nullptr);
        }
    } catch (std::exception const& e) {
        LOG_ERROR(Loader, "Error checking save data migration: {}", e.what());
    }
}

// Singleton storage
std::shared_ptr<UserSettingsImpl> UserSettingsImpl::s_instance = nullptr;
std::mutex UserSettingsImpl::s_mutex;

// Singleton
UserSettingsImpl::UserSettingsImpl() = default;

UserSettingsImpl::~UserSettingsImpl() {
    Save();
}

std::shared_ptr<UserSettingsImpl> UserSettingsImpl::GetInstance() {
    std::lock_guard lock(s_mutex);
    if (!s_instance)
        s_instance = std::make_shared<UserSettingsImpl>();
    return s_instance;
}

void UserSettingsImpl::SetInstance(std::shared_ptr<UserSettingsImpl> instance) {
    std::lock_guard lock(s_mutex);
    s_instance = std::move(instance);
}

bool UserSettingsImpl::Save() const {
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "users.json";
    try {
        json j;
        j["Users"] = m_userManager.GetUsers();
        j["Users"]["commit_hash"] = std::string(Common::g_scm_rev);

        std::ofstream out(path);
        if (!out) {
            LOG_ERROR(Config, "Failed to open user settings for writing: {}", path.string());
            return false;
        }
        out << std::setw(2) << j;
        return !out.fail();
    } catch (const std::exception& e) {
        LOG_ERROR(Config, "Error saving user settings: {}", e.what());
        return false;
    }
}

bool UserSettingsImpl::Load() {
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "users.json";
    try {
        if (!std::filesystem::exists(path)) {
            LOG_DEBUG(Config, "User settings file not found: {}", path.string());
            // Create default user if no file exists
            if (m_userManager.GetUsers().user.empty()) {
                m_userManager.GetUsers() = m_userManager.CreateDefaultUsers();
            }
            Save(); // Save default users
            CheckSaveAndTrophyMigration();
            return false;
        }

        std::ifstream in(path);
        if (!in) {
            LOG_ERROR(Config, "Failed to open user settings: {}", path.string());
            return false;
        }

        json j;
        in >> j;

        // Create a default Users object
        auto default_users = m_userManager.CreateDefaultUsers();

        // Convert default_users to json for merging
        json default_json;
        default_json["Users"] = default_users;

        // Merge the loaded json with defaults (preserves existing data, adds missing fields)
        if (j.contains("Users")) {
            json current = default_json["Users"];
            current.update(j["Users"]);
            m_userManager.GetUsers() = current.get<Users>();
        } else {
            m_userManager.GetUsers() = default_users;
        }

        if (m_userManager.GetUsers().commit_hash != Common::g_scm_rev) {
            Save();
        }

        LOG_DEBUG(Config, "User settings loaded successfully");
        CheckSaveAndTrophyMigration();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Config, "Error loading user settings: {}", e.what());
        // Fall back to defaults
        if (m_userManager.GetUsers().user.empty()) {
            m_userManager.GetUsers() = m_userManager.CreateDefaultUsers();
        }
        CheckSaveAndTrophyMigration();
        return false;
    }
}
