// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/file_sys/game_backend.h"

namespace Core::FileSys {

class HostDirectoryBackend final : public IGameBackend {
public:
    explicit HostDirectoryBackend(std::filesystem::path root);

    [[nodiscard]] bool Exists(std::string_view rel_path) const override;
    [[nodiscard]] bool IsDirectory(std::string_view rel_path) const override;
    [[nodiscard]] std::optional<std::vector<u8>> ReadFile(std::string_view rel_path) const override;
    [[nodiscard]] std::vector<DirEntry> ListDir(std::string_view rel_path) const override;
    [[nodiscard]] std::optional<std::filesystem::path> HostRootPath() const override {
        return m_root;
    }
    [[nodiscard]] std::filesystem::path RootPath() const override {
        return m_root;
    }
    [[nodiscard]] bool IsArchive() const override {
        return false;
    }
    [[nodiscard]] bool IsOpen() const override {
        return true;
    }

private:
    std::filesystem::path m_root;
};

} // namespace Core::FileSys
