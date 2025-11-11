// SPDX-FileCopyrightText: Copyright 2025 shadPS4-Qtlauncher Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include "common/types.h"
#include "user_manager.h"

// Keep JSON type definitions for User and Users centralized here
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(User, user_id, user_color, user_name, controller_port)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Users, default_user_id, user)

class EmulatorSettings {
public:
    EmulatorSettings();
    ~EmulatorSettings();

    bool Save() const;
    bool Load();

    UserManager& GetUserManager() {
        return m_userManager;
    }
    const UserManager& GetUserManager() const {
        return m_userManager;
    }

private:
    UserManager m_userManager;
};