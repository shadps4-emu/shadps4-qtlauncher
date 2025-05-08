// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <thread>

#include "common/singleton.h"

namespace Core {

class Emulator {
public:
    Emulator();
    ~Emulator();

    void Run(const std::filesystem::path& file, const std::vector<std::string> args = {});
    void UpdatePlayTime(const std::string& serial);

private:
    void LoadSystemModules(const std::string& game_serial);

    //Core::MemoryManager* memory;
    //Input::GameController* controller;
    //Core::Linker* linker;
    //std::unique_ptr<Frontend::WindowSDL> window;
    std::chrono::steady_clock::time_point start_time;
};

} // namespace Core
