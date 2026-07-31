// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <zarchive/zarchivereader.h>

#include "common/logging/log.h"
#include "core/file_sys/backends/zarchive_backend.h"

namespace Core::FileSys {

// ZArchive lookups do not accept a leading slash.
static std::string_view NormalizeRel(std::string_view rel) {
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) {
        rel.remove_prefix(1);
    }
    return rel;
}

ZArchiveGameBackend::ZArchiveGameBackend(std::filesystem::path archive_path)
    : m_archive_path(std::move(archive_path)) {
    m_reader = ZArchiveReader::OpenFromFile(m_archive_path);
    if (!m_reader) {
        LOG_ERROR(Common_Filesystem, "Failed to open ZArchive: {}", m_archive_path.string());
    }
}

ZArchiveGameBackend::~ZArchiveGameBackend() {
    delete m_reader;
}

bool ZArchiveGameBackend::Exists(std::string_view rel_path) const {
    if (!IsOpen()) {
        return false;
    }
    return m_reader->LookUp(NormalizeRel(rel_path), /*allow_file=*/true,
                            /*allow_directory=*/true) != ZARCHIVE_INVALID_NODE;
}

bool ZArchiveGameBackend::IsDirectory(std::string_view rel_path) const {
    if (!IsOpen()) {
        return false;
    }
    const auto node =
        m_reader->LookUp(NormalizeRel(rel_path), /*allow_file=*/false, /*allow_directory=*/true);
    return node != ZARCHIVE_INVALID_NODE && m_reader->IsDirectory(node);
}

std::optional<std::vector<u8>> ZArchiveGameBackend::ReadFile(std::string_view rel_path) const {
    if (!IsOpen()) {
        return std::nullopt;
    }
    const auto node =
        m_reader->LookUp(NormalizeRel(rel_path), /*allow_file=*/true, /*allow_directory=*/false);
    if (node == ZARCHIVE_INVALID_NODE || !m_reader->IsFile(node)) {
        return std::nullopt;
    }
    const u64 size = m_reader->GetFileSize(node);
    std::vector<u8> data(size);
    u64 total_read = 0;
    while (total_read < size) {
        const u64 got =
            m_reader->ReadFromFile(node, total_read, size - total_read, data.data() + total_read);
        if (got == 0) {
            break;
        }
        total_read += got;
    }
    if (total_read != size) {
        LOG_ERROR(Common_Filesystem, "Short read from ZArchive entry: {} ({}/{} bytes)", rel_path,
                  total_read, size);
        return std::nullopt;
    }
    return data;
}

std::vector<DirEntry> ZArchiveGameBackend::ListDir(std::string_view rel_path) const {
    std::vector<DirEntry> entries;
    if (!IsOpen()) {
        return entries;
    }
    const auto node =
        m_reader->LookUp(NormalizeRel(rel_path), /*allow_file=*/false, /*allow_directory=*/true);
    if (node == ZARCHIVE_INVALID_NODE || !m_reader->IsDirectory(node)) {
        return entries;
    }
    const u32 count = m_reader->GetDirEntryCount(node);
    entries.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        ZArchiveReader::DirEntry entry{};
        if (!m_reader->GetDirEntry(node, i, entry)) {
            continue;
        }
        entries.push_back(DirEntry{
            .name = std::string(entry.name.data(), entry.name.size()),
            .is_directory = entry.isDirectory,
        });
    }
    return entries;
}

} // namespace Core::FileSys
