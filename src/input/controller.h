// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <SDL3/SDL_gamepad.h>

namespace GamepadSelect {

int GetIndexfromGUID(SDL_JoystickID* gamepadIDs, int gamepadCount, std::string GUID);
std::string GetGUIDString(SDL_JoystickID* gamepadIDs, int index);
std::string GetSelectedGamepad();
void SetSelectedGamepad(std::string GUID);

} // namespace GamepadSelect
