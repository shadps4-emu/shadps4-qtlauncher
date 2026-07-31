// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/file_sys/game_backend.h"

class ZArchiveReader;

namespace Core::FileSys {

class ZArchiveGameBackend final : public IGameBackend {
public:
    explicit ZArchiveGameBackend(std::filesystem::path archive_path);
    ~ZArchiveGameBackend() override;

    ZArchiveGameBackend(const ZArchiveGameBackend&) = delete;
    ZArchiveGameBackend& operator=(const ZArchiveGameBackend&) = delete;

    [[nodiscard]] bool Exists(std::string_view rel_path) const override;
    [[nodiscard]] bool IsDirectory(std::string_view rel_path) const override;
    [[nodiscard]] std::optional<std::vector<u8>> ReadFile(std::string_view rel_path) const override;
    [[nodiscard]] std::vector<DirEntry> ListDir(std::string_view rel_path) const override;
    [[nodiscard]] std::optional<std::filesystem::path> HostRootPath() const override {
        return std::nullopt;
    }
    [[nodiscard]] std::filesystem::path RootPath() const override {
        return m_archive_path;
    }
    [[nodiscard]] bool IsArchive() const override {
        return true;
    }
    [[nodiscard]] bool IsOpen() const override {
        return m_reader != nullptr;
    }

private:
    std::filesystem::path m_archive_path;
    ZArchiveReader* m_reader{nullptr};
};

} // namespace Core::FileSys
