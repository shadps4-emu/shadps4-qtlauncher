// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/logging/backend.h"

#if defined(__x86_64__) || defined(_M_X64)
#define Crash() __asm__ __volatile__("int $3")
#elif defined(__aarch64__) || defined(_M_ARM64)
#define Crash() __asm__ __volatile__("brk 0")
#else
#error "Missing Crash() implementation for target CPU architecture."
#endif

void assert_fail_impl() {
    Common::Log::Stop();
    std::fflush(stdout);
    Crash();
}

[[noreturn]] void unreachable_impl() {
    Common::Log::Stop();
    std::fflush(stdout);
    Crash();
    throw std::runtime_error("Unreachable code");
}

void assert_fail_debug_msg(const char* msg) {
    LOG_CRITICAL(Debug, "Assertion Failed!\n{}", msg);
    assert_fail_impl();
}
