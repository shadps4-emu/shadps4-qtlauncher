// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <system_error>
#include <vector>

#include <zarchive/zarchivewriter.h>
#include "core/file_sys/game_backend.h"
#include "core/file_sys/zar_packer.h"

namespace Core::FileSys {

struct PackContext {
    std::filesystem::path output_path;
    std::ofstream current_output_file;
    bool has_error = false;
};

void NewOutputFileCallback(int32_t /*part_index*/, void* ctx) {
    auto* pack_context = static_cast<PackContext*>(ctx);
    pack_context->current_output_file.open(pack_context->output_path,
                                           std::ios::binary | std::ios::trunc);
    if (!pack_context->current_output_file.is_open()) {
        pack_context->has_error = true;
    }
}

void WriteOutputDataCallback(const void* data, size_t length, void* ctx) {
    auto* pack_context = static_cast<PackContext*>(ctx);
    if (!pack_context->current_output_file.is_open()) {
        pack_context->has_error = true;
        return;
    }
    pack_context->current_output_file.write(static_cast<const char*>(data),
                                            static_cast<std::streamsize>(length));
    if (!pack_context->current_output_file.good()) {
        pack_context->has_error = true;
    }
}

bool PackDirectoryToZArchive(const std::filesystem::path& input_dir,
                             const std::filesystem::path& output_zar,
                             const std::function<bool(const PackProgress&)>& progress_cb,
                             std::string* error_message) {
    namespace fs = std::filesystem;

    const auto fail = [&](const std::string& message) {
        if (error_message) {
            *error_message = message;
        }
        std::error_code rm_ec;
        fs::remove(output_zar, rm_ec);
        return false;
    };

    std::error_code ec;
    if (!fs::is_directory(input_dir, ec) || ec) {
        return fail("Input path is not a directory: " + input_dir.string());
    }

    PackProgress progress;
    for (const auto& entry : fs::recursive_directory_iterator(
             input_dir, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            break;
        }
        std::error_code entry_ec;
        if (entry.is_regular_file(entry_ec) && !entry_ec) {
            const auto size = entry.file_size(entry_ec);
            if (!entry_ec) {
                progress.bytes_total += size;
            }
        }
    }

    PackContext pack_context;
    pack_context.output_path = output_zar;

    std::error_code mkdir_ec;
    fs::create_directories(output_zar.parent_path(), mkdir_ec);

    ZArchiveWriter writer(NewOutputFileCallback, WriteOutputDataCallback, &pack_context);
    if (pack_context.has_error) {
        return fail("Failed to create output file: " + output_zar.string());
    }

    std::vector<u8> buffer(1 * 1024 * 1024);

    ec.clear();
    for (const auto& entry : fs::recursive_directory_iterator(
             input_dir, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            return fail("Failed to enumerate input directory: " + ec.message());
        }

        std::error_code rel_ec;
        const fs::path rel_path = fs::relative(entry.path(), input_dir, rel_ec);
        if (rel_ec) {
            return fail("Failed to resolve relative path for: " + entry.path().string());
        }
        const std::string archive_path = rel_path.generic_string();

        std::error_code type_ec;
        if (entry.is_directory(type_ec) && !type_ec) {
            if (!writer.MakeDir(archive_path.c_str(), false)) {
                return fail("Failed to create directory in archive: " + archive_path);
            }
            continue;
        }

        if (!entry.is_regular_file(type_ec) || type_ec) {
            continue; // skip symlinks/special files, matching upstream ZArchive's packer
        }

        std::error_code eq_ec;
        if (fs::equivalent(entry.path(), output_zar, eq_ec) && !eq_ec) {
            continue; // never try to pack the archive we're currently writing
        }

        progress.current_file = archive_path;
        if (progress_cb && !progress_cb(progress)) {
            return fail("Canceled");
        }

        if (!writer.StartNewFile(archive_path.c_str())) {
            return fail("Failed to add file to archive: " + archive_path);
        }

        std::ifstream in(entry.path(), std::ios::binary);
        if (!in.is_open()) {
            return fail("Failed to open input file: " + entry.path().string());
        }

        while (in) {
            in.read(reinterpret_cast<char*>(buffer.data()),
                    static_cast<std::streamsize>(buffer.size()));
            const auto read_bytes = in.gcount();
            if (read_bytes <= 0) {
                break;
            }

            writer.AppendData(buffer.data(), static_cast<size_t>(read_bytes));
            if (pack_context.has_error) {
                return fail("Write error while packing: " + archive_path);
            }

            progress.bytes_done += static_cast<u64>(read_bytes);
            if (progress_cb && !progress_cb(progress)) {
                return fail("Canceled");
            }
        }

        if (in.bad()) {
            return fail("Read error while packing: " + archive_path);
        }
    }

    writer.Finalize();
    pack_context.current_output_file.close();

    if (pack_context.has_error) {
        return fail("Write error while finalizing archive");
    }

    return true;
}

namespace {

void CollectZArchiveEntries(const IGameBackend& backend, const std::string& rel_dir,
                            std::vector<std::string>& out_files,
                            std::vector<std::string>& out_dirs) {
    for (const auto& entry : backend.ListDir(rel_dir)) {
        const std::string child = rel_dir.empty() ? entry.name : rel_dir + "/" + entry.name;
        if (entry.is_directory) {
            out_dirs.push_back(child);
            CollectZArchiveEntries(backend, child, out_files, out_dirs);
        } else {
            out_files.push_back(child);
        }
    }
}

// Reads a single archive entry and writes it out to output_dir/rel_path,
// creating any missing parent directories. Returns an error string on
// failure, or an empty string on success.
std::string ExtractOneFile(const IGameBackend& backend, const std::filesystem::path& output_dir,
                           const std::string& rel_path) {
    namespace fs = std::filesystem;

    const auto data = backend.ReadFile(rel_path);
    if (!data.has_value()) {
        return "Failed to read file from archive: " + rel_path;
    }

    const fs::path dest_path = output_dir / fs::path(rel_path);
    std::error_code dir_ec;
    fs::create_directories(dest_path.parent_path(), dir_ec);

    std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return "Failed to create output file: " + dest_path.string();
    }
    out.write(reinterpret_cast<const char*>(data->data()),
              static_cast<std::streamsize>(data->size()));
    if (!out.good()) {
        return "Write error while extracting: " + rel_path;
    }
    out.close();
    if (!out.good()) {
        return "Write error while extracting: " + rel_path;
    }
    return {};
}

} // namespace

bool UnpackZArchiveToDirectory(const std::filesystem::path& input_zar,
                               const std::filesystem::path& output_dir,
                               const std::function<bool(const UnpackProgress&)>& progress_cb,
                               std::string* error_message) {
    namespace fs = std::filesystem;

    const auto fail = [&](const std::string& message) {
        if (error_message) {
            *error_message = message;
        }
        return false;
    };

    if (!IsZArchiveFile(input_zar)) {
        return fail("Input path is not a ZArchive file: " + input_zar.string());
    }

    const auto backend = OpenGameBackend(input_zar);
    if (!backend || !backend->IsOpen()) {
        return fail("Failed to open ZArchive: " + input_zar.string());
    }

    std::error_code mkdir_ec;
    fs::create_directories(output_dir, mkdir_ec);
    if (mkdir_ec) {
        return fail("Failed to create output directory: " + output_dir.string());
    }

    std::vector<std::string> files;
    std::vector<std::string> dirs;
    CollectZArchiveEntries(*backend, "", files, dirs);

    for (const auto& rel_dir : dirs) {
        std::error_code dir_ec;
        fs::create_directories(output_dir / fs::path(rel_dir), dir_ec);
        if (dir_ec) {
            return fail("Failed to create directory: " + rel_dir);
        }
    }

    UnpackProgress progress;
    progress.files_total = files.size();

    for (const auto& rel_path : files) {
        progress.current_file = rel_path;
        if (progress_cb && !progress_cb(progress)) {
            return fail("Canceled");
        }

        if (const std::string error = ExtractOneFile(*backend, output_dir, rel_path);
            !error.empty()) {
            return fail(error);
        }

        progress.files_done++;
        if (progress_cb && !progress_cb(progress)) {
            return fail("Canceled");
        }
    }

    return true;
}

bool ExtractZArchiveFiles(const std::filesystem::path& input_zar,
                          const std::filesystem::path& output_dir,
                          const std::vector<std::string>& rel_paths,
                          const std::function<bool(const UnpackProgress&)>& progress_cb,
                          std::string* error_message) {
    namespace fs = std::filesystem;

    const auto fail = [&](const std::string& message) {
        if (error_message) {
            *error_message = message;
        }
        return false;
    };

    if (!IsZArchiveFile(input_zar)) {
        return fail("Input path is not a ZArchive file: " + input_zar.string());
    }
    if (rel_paths.empty()) {
        return fail("No files were selected for extraction.");
    }

    const auto backend = OpenGameBackend(input_zar);
    if (!backend || !backend->IsOpen()) {
        return fail("Failed to open ZArchive: " + input_zar.string());
    }

    std::error_code mkdir_ec;
    fs::create_directories(output_dir, mkdir_ec);
    if (mkdir_ec) {
        return fail("Failed to create output directory: " + output_dir.string());
    }

    UnpackProgress progress;
    progress.files_total = rel_paths.size();

    for (const auto& rel_path : rel_paths) {
        progress.current_file = rel_path;
        if (progress_cb && !progress_cb(progress)) {
            return fail("Canceled");
        }

        if (const std::string error = ExtractOneFile(*backend, output_dir, rel_path);
            !error.empty()) {
            return fail(error);
        }

        progress.files_done++;
        if (progress_cb && !progress_cb(progress)) {
            return fail("Canceled");
        }
    }

    return true;
}

} // namespace Core::FileSys
