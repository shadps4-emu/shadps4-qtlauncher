// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// #include <imgui.h>

#include "common/assert.h"
#include "common/singleton.h"
#include "debug_state.h"

using namespace DebugStateType;

DebugStateImpl& DebugState = *Common::Singleton<DebugStateImpl>::Instance();

bool DebugStateType::showing_debug_menu_bar = false;

static ThreadID ThisThreadID() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
}

static void PauseThread(ThreadID id) {
#ifdef _WIN32
    auto handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, id);
    SuspendThread(handle);
    CloseHandle(handle);
#else
    pthread_kill(id, SIGUSR1);
#endif
}

static void ResumeThread(ThreadID id) {
#ifdef _WIN32
    auto handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, id);
    ResumeThread(handle);
    CloseHandle(handle);
#else
    pthread_kill(id, SIGUSR1);
#endif
}
