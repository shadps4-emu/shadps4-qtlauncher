// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <SDL3/SDL.h>
#include "input/controller.h"

static std::string SelectedGamepad = "";

namespace GamepadSelect {

int GetIndexfromGUID(SDL_JoystickID* gamepadIDs, int gamepadCount, std::string GUID) {
    char GUIDbuf[33];
    for (int i = 0; i < gamepadCount; i++) {
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(gamepadIDs[i]), GUIDbuf, 33);
        std::string currentGUID = std::string(GUIDbuf);
        if (currentGUID == GUID) {
            return i;
        }
    }
    return -1;
}

std::string GetGUIDString(SDL_JoystickID* gamepadIDs, int index) {
    char GUIDbuf[33];
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(gamepadIDs[index]), GUIDbuf, 33);
    std::string GUID = std::string(GUIDbuf);
    return GUID;
}

std::string GetSelectedGamepad() {
    return SelectedGamepad;
}

void SetSelectedGamepad(std::string GUID) {
    SelectedGamepad = GUID;
}

} // namespace GamepadSelect
