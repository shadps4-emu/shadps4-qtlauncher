// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <queue>

#include "common/types.h"
// #include "video_core/renderer_vulkan/vk_graphics_pipeline.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <Windows.h>
using ThreadID = DWORD;
#else
#include <pthread.h>
#include <signal.h>
using ThreadID = pthread_t;
#endif

namespace Core::Devtools {
class Layer;
namespace Widget {
class FrameGraph;
class ShaderList;
} // namespace Widget
} // namespace Core::Devtools

namespace DebugStateType {

extern bool showing_debug_menu_bar;

class DebugStateImpl {
    friend class Core::Devtools::Layer;
    friend class Core::Devtools::Widget::FrameGraph;
    friend class Core::Devtools::Widget::ShaderList;

    std::queue<std::string> debug_message_popup;

    std::mutex guest_threads_mutex{};
    std::vector<ThreadID> guest_threads{};
    std::atomic_bool is_guest_threads_paused = false;
    u64 pause_time{};

    std::atomic_int32_t flip_frame_count = 0;
    std::atomic_int32_t gnm_frame_count = 0;

    s32 gnm_frame_dump_request_count = -1;
    std::unordered_map<size_t, std::string> waiting_reg_dumps_dbg;
    bool waiting_submit_pause = false;
    bool should_show_frame_dump = false;

    std::shared_mutex frame_dump_list_mutex;

public:
    float Framerate = 1.0f / 60.0f;
    float FrameDeltaTime;

    std::pair<u32, u32> game_resolution{};
    std::pair<u32, u32> output_resolution{};
    bool is_using_fsr{};

    void ShowDebugMessage(std::string message) {
        if (message.empty()) {
            return;
        }
        debug_message_popup.push(std::move(message));
    }

    bool& IsShowingDebugMenuBar() {
        return showing_debug_menu_bar;
    }

    bool IsGuestThreadsPaused() const {
        return is_guest_threads_paused;
    }
};
} // namespace DebugStateType

extern DebugStateType::DebugStateImpl& DebugState;
