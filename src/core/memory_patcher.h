#pragma once

#include <string>
#include <filesystem>

#include "common/memory_patcher.h"

namespace MemoryPatcher {


struct PendingPatch {
    std::string modName;
    std::string address;
    std::string value;
    std::string target;
    std::string size;
    bool        littleEndian = false;
    PatchMask   mask         = PatchMask::None;
    int         maskOffset   = 0;
};

std::vector<PendingPatch> readPatches(std::string gameSerial, std::string appVersion);
}