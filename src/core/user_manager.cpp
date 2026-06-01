// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <iostream>
#include <QFile>
#include <QMessageBox>
#include <QXmlStreamReader>
#include <common/assert.h>
#include <common/path_util.h>
#include "emulator_settings.h"
#include "qt_gui/main_window.h"
#include "user_manager.h"
#include "user_settings.h"

namespace fs = std::filesystem;

#define tr(...) MainWindow::tr(__VA_ARGS__)

bool UserManager::AddUser(const User& user) {
    for (const auto& u : m_users.user) {
        if (u.user_id == user.user_id)
            return false; // already exists
    }

    m_users.user.push_back(user);

    // Create user home directory and subfolders
    const auto user_dir = EmulatorSettings.GetHomeDir() / std::to_string(user.user_id);

    std::error_code ec;
    if (!std::filesystem::exists(user_dir)) {
        std::filesystem::create_directories(user_dir, ec);
        std::filesystem::create_directories(user_dir / "savedata", ec);
        std::filesystem::create_directories(user_dir / "trophy", ec);
        std::filesystem::create_directories(user_dir / "inputs", ec);
    }

    Save();
    return true;
}

bool UserManager::RemoveUser(s32 user_id) {
    auto it = std::remove_if(m_users.user.begin(), m_users.user.end(),
                             [user_id](const User& u) { return u.user_id == user_id; });
    if (it == m_users.user.end())
        return false; // not found

    const auto user_dir = EmulatorSettings.GetHomeDir() / std::to_string(user_id);

    if (std::filesystem::exists(user_dir)) {
        std::error_code ec;
        std::filesystem::remove_all(user_dir, ec);
    }

    m_users.user.erase(it, m_users.user.end());
    Save();
    return true;
}

bool UserManager::RenameUser(s32 user_id, const std::string& new_name) {
    // Find user in the internal list
    for (auto& user : m_users.user) {
        if (user.user_id == user_id) {
            if (user.user_name == new_name)
                return true; // no change

            user.user_name = new_name;
            return true;
        }
    }
    Save();
    return false;
}

User* UserManager::GetUserByID(s32 user_id) {
    for (auto& u : m_users.user) {
        if (u.user_id == user_id)
            return &u;
    }
    return nullptr;
}

User* UserManager::GetUserByPlayerIndex(s32 index) {
    for (auto& u : m_users.user) {
        if (u.player_index == index)
            return &u;
    }
    return nullptr;
}

const std::vector<User>& UserManager::GetAllUsers() const {
    return m_users.user;
}

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

static void MoveFolder(fs::path const& _from, fs::path const& _to) {
    try {
        fs::rename(_from, _to);
    } catch (...) {
        fs::copy(_from, _to, fs::copy_options::recursive);
        fs::remove_all(_from);
    }
}

static void CheckAndMigrateSaves(TransferOption option) {
    auto const new_save_dir = EmulatorSettings.GetHomeDir() / "1000" / "savedata";
    auto const old_save_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "savedata" / "1";
    if (fs::exists(old_save_dir) && !fs::is_empty(old_save_dir)) {
        try {
            switch (option) {
            case TransferOption::Copy:
                fs::copy(old_save_dir, new_save_dir, fs::copy_options::recursive);
                break;
            case TransferOption::Move:
                MoveFolder(old_save_dir, new_save_dir);
                break;
            case TransferOption::MoveAndLinkBack:
                MoveFolder(old_save_dir, new_save_dir);
                fs::create_directory_symlink(new_save_dir, old_save_dir);
                break;
            case TransferOption::SdlCancelled:
            case TransferOption::Nothing:
                break;
            default:
                UNREACHABLE();
            }
        } catch (std::exception const& e) {
            UNREACHABLE_MSG("Error while migrating saves: {}", e.what());
        }
    }
}

static void CheckAndMigrateTrophies(TransferOption option) {
    auto const user_dir = EmulatorSettings.GetHomeDir() / "1000";
    auto const old_trophy_base_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "game_data";
    auto const new_trophy_global_dir =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "trophy";
    try {
        for (auto const& entry : fs::directory_iterator(old_trophy_base_dir)) {
            if (!entry.is_directory()) {
                continue;
            }
            if (!fs::exists(entry.path() / "TrophyFiles")) {
                continue;
            }
            for (auto const& subentry : fs::directory_iterator(entry.path() / "TrophyFiles")) {
                if (!subentry.is_directory()) {
                    continue;
                }
                auto const old_trophy_dir = subentry.path();
                if (fs::exists(old_trophy_dir / "Xml")) {
                    QFile file(
                        QString::fromStdString((old_trophy_dir / "Xml" / "TROP.XML").string()));
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
                    if (fs::exists(user_dir / "trophy" / (npcommid + ".xml"))) {
                        continue;
                    }
                    if (!fs::exists(new_trophy_global_dir / npcommid)) {
                        fs::create_directories(new_trophy_global_dir / npcommid);
                        fs::copy(old_trophy_dir, new_trophy_global_dir / npcommid,
                                 fs::copy_options::recursive);
                    }
                    auto const old_trophy_file = old_trophy_dir / "Xml" / "TROP.XML";
                    auto const new_trophy_file = user_dir / "trophy" / (npcommid + ".xml");
                    switch (option) {
                    case TransferOption::Copy:
                        fs::copy_file(old_trophy_file, new_trophy_file);
                        break;
                    case TransferOption::Move:
                        MoveFolder(old_trophy_file, new_trophy_file);
                        break;
                    case TransferOption::MoveAndLinkBack:
                        MoveFolder(old_trophy_file, new_trophy_file);
                        fs::create_symlink(new_trophy_file, old_trophy_file);
                        break;
                    case TransferOption::Nothing:
                    case TransferOption::SdlCancelled:
                        break;
                    default:
                        UNREACHABLE();
                    }
                }
            }
        }
    } catch (std::exception const& e) {
        UNREACHABLE_MSG("Error while migrating trophies: {}", e.what());
    }
}

Users UserManager::CreateDefaultUsers() {
    Users default_users;
    default_users.user = {
        {
            .user_id = 1000,
            .user_name = "shadPS4",
            .user_color = 1,
            .player_index = 1,
            .shadnet_npid = "",
            .shadnet_password = "",
            .shadnet_token = "",
            .shadnet_email = "",
            .shadnet_enabled = false,
        },
        {
            .user_id = 1001,
            .user_name = "shadPS4-2",
            .user_color = 2,
            .player_index = 2,
            .shadnet_npid = "",
            .shadnet_password = "",
            .shadnet_token = "",
            .shadnet_email = "",
            .shadnet_enabled = false,
        },
        {
            .user_id = 1002,
            .user_name = "shadPS4-3",
            .user_color = 3,
            .player_index = 3,
            .shadnet_npid = "",
            .shadnet_password = "",
            .shadnet_token = "",
            .shadnet_email = "",
            .shadnet_enabled = false,
        },
        {
            .user_id = 1003,
            .user_name = "shadPS4-4",
            .user_color = 4,
            .player_index = 4,
            .shadnet_npid = "",
            .shadnet_password = "",
            .shadnet_token = "",
            .shadnet_email = "",
            .shadnet_enabled = false,
        },
    };

    for (auto& u : default_users.user) {
        const auto user_dir = EmulatorSettings.GetHomeDir() / std::to_string(u.user_id);

        if (!std::filesystem::exists(user_dir)) {
            std::filesystem::create_directory(user_dir);
            std::filesystem::create_directory(user_dir / "savedata");
            std::filesystem::create_directory(user_dir / "trophy");
            std::filesystem::create_directory(user_dir / "inputs");
            if (u.user_id == 1000) {
                TransferOption user_choice = AskMigrationOption();
                CheckAndMigrateSaves(user_choice);
                CheckAndMigrateTrophies(user_choice);
            }
        }
    }

    return default_users;
}

bool UserManager::SetDefaultUser(u32 user_id) {
    auto it = std::find_if(m_users.user.begin(), m_users.user.end(),
                           [user_id](const User& u) { return u.user_id == user_id; });
    if (it == m_users.user.end())
        return false;

    SetControllerPort(user_id, 1); // Set default user to port 1
    return Save();
}

User UserManager::GetDefaultUser() {
    return *GetUserByPlayerIndex(1);
}

void UserManager::SetControllerPort(u32 user_id, int port) {
    for (auto& u : m_users.user) {
        if (u.user_id != user_id && u.player_index == port)
            u.player_index = -1;
        if (u.user_id == user_id)
            u.player_index = port;
    }
    Save();
}
// Returns a list of users that have valid home directories
std::vector<User> UserManager::GetValidUsers() const {
    std::vector<User> result;
    result.reserve(m_users.user.size());

    const auto home_dir = EmulatorSettings.GetHomeDir();

    for (const auto& user : m_users.user) {
        const auto user_dir = home_dir / std::to_string(user.user_id);
        if (std::filesystem::exists(user_dir)) {
            result.push_back(user);
        }
    }

    return result;
}

LoggedInUsers UserManager::GetLoggedInUsers() const {
    return logged_in_users;
}

bool UserManager::Save() const {
    return UserSettings.Save();
}
