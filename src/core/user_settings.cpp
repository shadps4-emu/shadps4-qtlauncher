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
    std::error_code ec;
    fs::rename(_from, _to, ec);
    if (ec) {
        fs::copy(_from, _to, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
        if (!ec) {
            fs::remove_all(_from, ec);
        }
    }
}

static bool DirectoryContainsFiles(const fs::path& dir, std::error_code& ec) {
    if (!fs::exists(dir, ec) || fs::is_symlink(dir, ec)) {
        return false;
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_directory(ec)) {
            return true;
        }
    }
    return false;
}

static void CleanEmptyDirectories(const fs::path& root) {
    std::error_code ec;
    if (!fs::exists(root, ec) || fs::is_symlink(root, ec)) {
        return;
    }
    bool has_files = false;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) {
            has_files = true;
            break;
        }
    }
    if (!has_files) {
        fs::remove_all(root, ec);
    }
}

static void TransferDirectoryContents(const fs::path& source_dir, const fs::path& target_dir,
                                      TransferOption option) {
    std::error_code ec;
    if (!fs::exists(source_dir, ec) || fs::is_symlink(source_dir, ec)) {
        return;
    }
    fs::create_directories(target_dir, ec);

    for (const auto& entry : fs::directory_iterator(source_dir, ec)) {
        if (entry.is_symlink(ec)) {
            continue;
        }
        const auto src_path = entry.path();
        const auto dst_path = target_dir / src_path.filename();

        if (entry.is_directory(ec)) {
            if (!fs::exists(dst_path, ec)) {
                switch (option) {
                case TransferOption::Copy:
                    fs::copy(src_path, dst_path, fs::copy_options::recursive, ec);
                    break;
                case TransferOption::Move:
                    MovePath(src_path, dst_path);
                    break;
                case TransferOption::MoveAndLinkBack:
                    MovePath(src_path, dst_path);
                    fs::create_directory_symlink(dst_path, src_path, ec);
                    break;
                default:
                    break;
                }
            } else {
                TransferDirectoryContents(src_path, dst_path, option);
                if (option == TransferOption::MoveAndLinkBack && fs::is_empty(src_path, ec)) {
                    fs::remove(src_path, ec);
                    fs::create_directory_symlink(dst_path, src_path, ec);
                }
            }
        } else {
            if (!fs::exists(dst_path, ec)) {
                switch (option) {
                case TransferOption::Copy:
                    fs::copy_file(src_path, dst_path, ec);
                    break;
                case TransferOption::Move:
                    MovePath(src_path, dst_path);
                    break;
                case TransferOption::MoveAndLinkBack:
                    MovePath(src_path, dst_path);
                    fs::create_symlink(dst_path, src_path, ec);
                    break;
                default:
                    break;
                }
            }
        }
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

    msgbox.setWindowTitle(tr("User Data Migration"));
#ifdef _WIN32
    msgbox.setText(tr(
        "The shadPS4 user data directory structure has been updated to match the PS4 system "
        "format, and existing profile data has been detected in legacy locations.\n\n"
        "After migration, rebuilding the save database will be required for the emulator to "
        "detect your saves. You will be prompted to rebuild it, or you can run 'Rebuild Save "
        "Database' from the File menu at any time.\n\n"
        "Do you wish to copy your data over, move it over, or continue without doing anything?"));
#else
    msgbox.setText(
        tr("The shadPS4 user data directory structure has been updated to match the PS4 system "
           "format, and existing profile data has been detected in legacy locations.\n\n"
           "After migration, rebuilding the save database will be required for the emulator to "
           "detect your saves. You will be prompted to rebuild it, or you can run 'Rebuild Save "
           "Database' from the File menu at any time.\n\n"
           "Do you wish to copy your data over, move it over, move and link back to the original "
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
    const auto migration_done_path = home_dir / ".user_data_migration.complete";
    std::error_code ec;

    if (fs::exists(migration_done_path, ec)) {
        return false;
    }

    // Decimal user paths
    for (const auto& user : UserSettings.GetUserManager().GetAllUsers()) {
        if (user.user_id < 0) {
            continue;
        }
        const auto old_user_dir = home_dir / std::to_string(user.user_id);
        const auto new_user_dir = home_dir / FormatUidHex(user.user_id);
        if (old_user_dir != new_user_dir && DirectoryContainsFiles(old_user_dir, ec)) {
            return true;
        }
    }

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
                        if (target_name != name && DirectoryContainsFiles(entry.path(), ec)) {
                            return true;
                        }
                    }
                } catch (...) {
                }
            }
        }
    }

    // Legacy pre-multiuser path: savedata/1
    const auto old_dino_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "savedata" / "1";
    if (DirectoryContainsFiles(old_dino_dir, ec)) {
        return true;
    }

    // Legacy trophy directory
    const auto old_trophy_base_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "game_data";
    if (DirectoryContainsFiles(old_trophy_base_dir, ec)) {
        return true;
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
    LOG_INFO(Loader, "Starting unified user data migration");
    const auto home_dir = EmulatorSettings.GetHomeDir();
    const auto migration_done_path = home_dir / ".user_data_migration.complete";
    const auto legacy_marker = home_dir / ".savedata_migration.complete";
    std::error_code ec;

    TransferOption user_choice = AskSaveDataMigrationOption(parent);
    if (user_choice == TransferOption::Nothing || user_choice == TransferOption::SdlCancelled) {
        LOG_INFO(Loader, "User data migration cancelled or deferred by user");
        return false;
    }

    std::vector<std::pair<fs::path, fs::path>> decimal_pairs;

    // Decimal paths have priority over legacy savedata/1
    for (const auto& user : UserSettings.GetUserManager().GetAllUsers()) {
        if (user.user_id < 0) {
            continue;
        }
        const auto old_user_dir = home_dir / std::to_string(user.user_id);
        const auto new_user_dir = home_dir / FormatUidHex(user.user_id);
        if (old_user_dir != new_user_dir && fs::exists(old_user_dir, ec) &&
            !fs::is_symlink(old_user_dir, ec)) {
            if (std::find_if(decimal_pairs.begin(), decimal_pairs.end(), [&](const auto& p) {
                    return p.first == old_user_dir;
                }) == decimal_pairs.end()) {
                decimal_pairs.emplace_back(old_user_dir, new_user_dir);
            }
        }
    }

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
                            const auto old_user_dir = entry.path();
                            const auto new_user_dir = home_dir / target_name;
                            if (fs::exists(old_user_dir, ec) && !fs::is_symlink(old_user_dir, ec)) {
                                if (std::find_if(decimal_pairs.begin(), decimal_pairs.end(),
                                                 [&](const auto& p) {
                                                     return p.first == old_user_dir;
                                                 }) == decimal_pairs.end()) {
                                    decimal_pairs.emplace_back(old_user_dir, new_user_dir);
                                }
                            }
                        }
                    }
                } catch (...) {
                }
            }
        }
    }

    for (const auto& [source_root, target_root] : decimal_pairs) {
        LOG_INFO(Loader, "Migrating user profile from {} to {}", source_root.string(),
                 target_root.string());
        TransferDirectoryContents(source_root, target_root, user_choice);

        if (user_choice == TransferOption::Move) {
            CleanEmptyDirectories(source_root);
        } else if (user_choice == TransferOption::MoveAndLinkBack) {
            if (fs::exists(source_root, ec) && fs::is_empty(source_root, ec)) {
                fs::remove(source_root, ec);
                fs::create_directory_symlink(target_root, source_root, ec);
            }
        }
    }

    // Legacy savedata/1 path
    const auto old_dino_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "savedata" / "1";
    const auto dino_target = home_dir / FormatUidHex(1000) / "savedata";
    if (fs::exists(old_dino_dir, ec) && !fs::is_symlink(old_dino_dir, ec)) {
        LOG_INFO(Loader, "Checking legacy save data from {}", old_dino_dir.string());
        TransferDirectoryContents(old_dino_dir, dino_target, user_choice);

        if (user_choice == TransferOption::Move) {
            CleanEmptyDirectories(old_dino_dir);
            CleanEmptyDirectories(old_dino_dir.parent_path());
        } else if (user_choice == TransferOption::MoveAndLinkBack) {
            if (fs::exists(old_dino_dir, ec) && fs::is_empty(old_dino_dir, ec)) {
                fs::remove(old_dino_dir, ec);
                fs::create_directory_symlink(dino_target, old_dino_dir, ec);
            }
        }
    }

    // Migrate trophies if any exist in old game_data location
    CheckAndMigrateTrophies(user_choice);
    const auto old_trophy_base_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "game_data";
    if (user_choice == TransferOption::Move) {
        CleanEmptyDirectories(old_trophy_base_dir);
    }

    // Mark migration complete
    std::ofstream ofs(migration_done_path);
    ofs << "";
    if (fs::exists(legacy_marker, ec)) {
        fs::remove(legacy_marker, ec);
    }

    g_rebuild_prompted = true;
    QMessageBox rebuild_box(parent);
    rebuild_box.setWindowTitle(tr("Rebuild Save Database"));
    rebuild_box.setText(tr("User data migration completed.\n"
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
        warn.setText(tr("Unmigrated user profile data was detected.\n"
                        "Migration must be performed before the save database can be rebuilt.\n\n"
                        "Do you want to migrate your user data now?"));
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
        QMessageBox::critical(
            parent, tr("Rebuild Save Database"),
            tr("Failed to rebuild save data database. Ensure your emulator build supports database "
               "rebuilding."));
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
