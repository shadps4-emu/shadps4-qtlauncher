// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apple.h"

#include <Metal/Metal.h>

std::string GetAppleGpuName() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        std::string name([device.name UTF8String]);
        return name;
    }
}
