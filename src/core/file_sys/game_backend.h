// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"

namespace Core::FileSys {

struct DirEntry {
    std::string name;
    bool is_directory{false};
};

class IGameBackend {
public:
    virtual ~IGameBackend() = default;

    [[nodiscard]] virtual bool Exists(std::string_view rel_path) const = 0;
    [[nodiscard]] virtual bool IsDirectory(std::string_view rel_path) const = 0;

    // Reads an entire file into memory. Returns nullopt if it doesn't
    // exist or isn't a file.
    [[nodiscard]] virtual std::optional<std::vector<u8>> ReadFile(
        std::string_view rel_path) const = 0;

    // Lists immediate children of rel_path ("" for the root). Returns an
    // empty vector if rel_path doesn't exist or isn't a directory.
    [[nodiscard]] virtual std::vector<DirEntry> ListDir(std::string_view rel_path) const = 0;

    [[nodiscard]] virtual std::optional<std::filesystem::path> HostRootPath() const = 0;
    [[nodiscard]] virtual std::filesystem::path RootPath() const = 0;
    [[nodiscard]] virtual bool IsArchive() const = 0;
    [[nodiscard]] virtual bool IsOpen() const = 0;
};

// True if path is a regular file with a ".zar" extension
[[nodiscard]] bool IsZArchiveFile(const std::filesystem::path& path);

// Strips a trailing ".zar" extension so overlay suffixes ("-UPDATE", "-patch")
// can be appended to the game's stem regardless of the container type.
[[nodiscard]] std::filesystem::path StripZArchiveExtension(const std::filesystem::path& path);

// Resolves a candidate game/overlay root to an existing directory or .zar
// archive file
[[nodiscard]] std::optional<std::filesystem::path> ResolveGameRoot(
    const std::filesystem::path& root);

// Opens the appropriate backend for root
[[nodiscard]] std::unique_ptr<IGameBackend> OpenGameBackend(const std::filesystem::path& root);

[[nodiscard]] std::optional<std::vector<u8>> ReadGameFile(const std::filesystem::path& game_root,
                                                          std::string_view rel_path);

// Returns a real path on the host filesystem for rel_path inside game_root.
// For directory-backed games this is just game_root / rel_path; for archives
// the entry is extracted into the cache directory first so that code which
// requires a real file (QImage, QMediaPlayer, PSF::Open, ...) keeps working.
[[nodiscard]] std::optional<std::filesystem::path> ResolveGameFilePath(
    const std::filesystem::path& game_root, std::string_view rel_path);

// Total size in bytes of a game root: the archive's own file size for .zar,
// otherwise the recursive size of the directory.
[[nodiscard]] u64 GetGameRootSize(const std::filesystem::path& game_root);

} // namespace Core::FileSys
