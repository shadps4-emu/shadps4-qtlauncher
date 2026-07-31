// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <system_error>

#include "core/file_sys/backends/host_directory_backend.h"

namespace Core::FileSys {

HostDirectoryBackend::HostDirectoryBackend(std::filesystem::path root) : m_root(std::move(root)) {}

bool HostDirectoryBackend::Exists(std::string_view rel_path) const {
    std::error_code ec;
    return std::filesystem::exists(m_root / rel_path, ec) && !ec;
}

bool HostDirectoryBackend::IsDirectory(std::string_view rel_path) const {
    std::error_code ec;
    return std::filesystem::is_directory(m_root / rel_path, ec) && !ec;
}

std::optional<std::vector<u8>> HostDirectoryBackend::ReadFile(std::string_view rel_path) const {
    const std::filesystem::path full_path = m_root / rel_path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(full_path, ec) || ec) {
        return std::nullopt;
    }

    std::ifstream in(full_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return std::nullopt;
    }
    const auto size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    std::vector<u8> data(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in.good() && !in.eof()) {
        return std::nullopt;
    }
    return data;
}

std::vector<DirEntry> HostDirectoryBackend::ListDir(std::string_view rel_path) const {
    std::vector<DirEntry> entries;
    std::error_code ec;
    const std::filesystem::path dir_path = m_root / rel_path;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
        if (ec) {
            break;
        }
        std::error_code entry_ec;
        const bool is_dir = entry.is_directory(entry_ec);
        entries.push_back(DirEntry{
            .name = entry.path().filename().string(),
            .is_directory = !entry_ec && is_dir,
        });
    }
    return entries;
}

} // namespace Core::FileSys
