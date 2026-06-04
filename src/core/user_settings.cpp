// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <QFile>
#include <QMessageBox>
#include <QXmlStreamReader>
#include <common/path_util.h>
#include <common/scm_rev.h>
#include "common/assert.h"
#include "common/logging/log.h"
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

TransferOption AskMigrationOption(QWidget* parent = nullptr) {
    QMessageBox msgbox(parent);

    // clang-format off
    msgbox.setWindowTitle(tr("Save/Trophy Migration"));
#ifdef _WIN32
    msgbox.setText(tr("The shadPS4 save and trophy locations have been updated, and save/trophy files have been detected in the old location.\n") +
                   tr("Do you wish to copy them over, move them over, or continue without doing anything?"));
#else
    msgbox.setText(tr("The shadPS4 save and trophy locations have been updated, and save/trophy files have been detected in the old location.\n") +
                   tr("Do you wish to copy them over, move them over, move and link back to the original location, or continue without doing anything?"));
#endif
    // clang-format on

    auto* copy_btn = (QAbstractButton*)msgbox.addButton("Copy", QMessageBox::AcceptRole);
    auto* move_btn = (QAbstractButton*)msgbox.addButton("Move", QMessageBox::AcceptRole);

#ifndef _WIN32
    auto* link_btn =
        (QAbstractButton*)msgbox.addButton("Move and link back", QMessageBox::AcceptRole);
#endif

    auto* nothing_btn = (QAbstractButton*)msgbox.addButton("Do nothing", QMessageBox::RejectRole);

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

static void MovePath(fs::path const& _from, fs::path const& _to) {
    try {
        fs::rename(_from, _to);
    } catch (...) {
        fs::copy(_from, _to, fs::copy_options::recursive | fs::copy_options::skip_existing);
        // no delete to avoid data loss
    }
}

static void CheckAndMigrateSaves(TransferOption option) {
    LOG_INFO(Loader, "Starting save migration");
    auto const new_save_root = EmulatorSettings.GetHomeDir() / "1000" / "savedata";
    auto const old_save_root =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "savedata" / "1";
    if (!std::filesystem::exists(old_save_root)) {
        return;
    }
    for (const auto& entry : fs::directory_iterator(old_save_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto old_game_dir = entry.path();
        const auto new_game_dir = new_save_root / old_game_dir.filename();
        const bool already_exists = fs::exists(new_game_dir);
        LOG_INFO(Loader, "Transferring {}", old_game_dir);

        switch (option) {
        case TransferOption::Copy:
            if (!already_exists) {
                fs::copy(old_game_dir, new_game_dir, fs::copy_options::recursive);
            }
            break;
        case TransferOption::Move:
            if (!already_exists) {
                MovePath(old_game_dir, new_game_dir);
            }
            break;
        case TransferOption::MoveAndLinkBack:
            if (!already_exists) {
                MovePath(old_game_dir, new_game_dir);
                fs::create_directory_symlink(new_game_dir, old_game_dir);
            }
            break;
        case TransferOption::Nothing:
        case TransferOption::SdlCancelled:
            return;
        default:
            UNREACHABLE();
        }
    }
    LOG_INFO(Loader, "Save migration complete");
}

static void CheckAndMigrateTrophies(TransferOption option) {
    LOG_INFO(Loader, "Starting trophy migration");
    const auto user_dir = EmulatorSettings.GetHomeDir() / "1000";
    const auto old_trophy_base_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "game_data";
    const auto new_trophy_global_dir = Common::FS::GetUserPath(Common::FS::PathType::TrophyDir);
    if (!fs::exists(old_trophy_base_dir)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(old_trophy_base_dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto trophy_files_dir = entry.path() / "TrophyFiles";
        LOG_INFO(Loader, "Transferring {}", trophy_files_dir);

        if (!fs::exists(trophy_files_dir)) {
            continue;
        }

        for (const auto& subentry : fs::directory_iterator(trophy_files_dir)) {
            if (!subentry.is_directory()) {
                continue;
            }

            const auto old_trophy_dir = subentry.path();
            LOG_INFO(Loader, "Transferring {}", old_trophy_dir);

            const auto xml_path = old_trophy_dir / "Xml" / "TROP.XML";
            if (!fs::exists(xml_path)) {
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
            if (fs::exists(new_trophy_file)) {
                continue;
            }

            const auto new_trophy_dir = new_trophy_global_dir / npcommid;
            if (!fs::exists(new_trophy_dir)) {
                fs::create_directories(new_trophy_dir);
                fs::copy(old_trophy_dir, new_trophy_dir, fs::copy_options::recursive);
            }

            switch (option) {
            case TransferOption::Copy:
                fs::copy_file(xml_path, new_trophy_file);
                break;
            case TransferOption::Move:
                MovePath(xml_path, new_trophy_file);
                break;
            case TransferOption::MoveAndLinkBack:
                MovePath(xml_path, new_trophy_file);
                fs::create_symlink(new_trophy_file, xml_path);
                break;
            case TransferOption::Nothing:
            case TransferOption::SdlCancelled:
                return;
            default:
                UNREACHABLE();
            }
        }
    }
    LOG_INFO(Loader, "Trophy migration complete");
}

void CheckSaveAndTrophyMigration() {
    auto const migration_done_path =
        EmulatorSettings.GetHomeDir() / "1000" / "savedata" / ".data_transfer.complete";
    auto const old_save_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "savedata" / "1";
    try {
        if (fs::exists(old_save_dir) && !fs::is_empty(old_save_dir) &&
            !fs::exists(migration_done_path)) {
            TransferOption user_choice = AskMigrationOption();
            if (user_choice != TransferOption::Nothing &&
                user_choice != TransferOption::SdlCancelled) {
                CheckAndMigrateSaves(user_choice);
                CheckAndMigrateTrophies(user_choice);
            }
            std::ofstream ofs(migration_done_path);
            ofs << "";
        }
    } catch (std::exception const& e) {
        QMessageBox::information(nullptr, "Save/Trophy migration",
                                 QString("Error while transferring data: %1").arg(e.what()));
        UNREACHABLE();
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