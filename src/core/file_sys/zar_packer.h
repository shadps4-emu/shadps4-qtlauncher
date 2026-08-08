// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "common/types.h"

namespace Core::FileSys {

struct PackProgress {
    u64 bytes_done = 0;
    u64 bytes_total = 0;
    std::string current_file;
};

bool PackDirectoryToZArchive(const std::filesystem::path& input_dir,
                             const std::filesystem::path& output_zar,
                             const std::function<bool(const PackProgress&)>& progress_cb,
                             std::string* error_message);

struct UnpackProgress {
    u64 files_done = 0;
    u64 files_total = 0;
    std::string current_file;
};

// Extracts every entry of a .zar archive into output_dir (created if needed).
bool UnpackZArchiveToDirectory(const std::filesystem::path& input_zar,
                               const std::filesystem::path& output_dir,
                               const std::function<bool(const UnpackProgress&)>& progress_cb,
                               std::string* error_message);

// Extracts only the given archive-relative file paths (forward-slash separated,
// as returned by IGameBackend::ListDir) into output_dir, preserving their
// relative directory structure.
bool ExtractZArchiveFiles(const std::filesystem::path& input_zar,
                          const std::filesystem::path& output_dir,
                          const std::vector<std::string>& rel_paths,
                          const std::function<bool(const UnpackProgress&)>& progress_cb,
                          std::string* error_message);

} // namespace Core::FileSys
