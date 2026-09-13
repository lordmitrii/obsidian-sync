#include "scanner.hpp"
#include "hasher.hpp"

#include <chrono>

namespace fs = std::filesystem;

bool should_ignore(const fs::path &path) {
    if (path.filename() == ".DS_Store")
        return true;

    bool in_obsidian_dir = false;
    bool in_trash_dir = false;

    for (const auto &part : path) {
        if (part == ".obsidian")
            in_obsidian_dir = true;
        if (part == ".trash")
            in_trash_dir = true;
    }

    if (in_trash_dir)
        return true;

    if (in_obsidian_dir) {
        const std::string filename = path.filename().string();

        if (filename == "workspace.json" || filename == "workspace-mobile.json")
            return true;

        for (const auto &part : path) {
            if (part == "cache")
                return true;
        }
    }

    if (path.filename().string().find(".conflict-remote") != std::string::npos)
        return true;

    if (path.extension() == ".tmp")
        return true;
    if (path.extension() == ".swp")
        return true;

    return false;
}

std::int64_t get_modified_time(const fs::path &path) {
    auto time = fs::last_write_time(path);

    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch());

    return seconds.count();
}

std::optional<FileMeta> scan_file(const fs::path &root, const fs::path &relative_path) {
    fs::path full_path = root / relative_path;

    if (!fs::exists(full_path) || !fs::is_regular_file(full_path) || should_ignore(full_path)) {
        return std::nullopt;
    }

    FileMeta meta;
    meta.path = relative_path.generic_string();
    meta.size = fs::file_size(full_path);
    meta.modified_time = get_modified_time(full_path);
    meta.hash = sha256_file(full_path);

    return meta;
}

std::vector<FileMeta> scan_vault(const fs::path &vault_path) {
    std::vector<FileMeta> files;

    for (const auto &entry : fs::recursive_directory_iterator(vault_path)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        fs::path full_path = entry.path();

        if (should_ignore(full_path)) {
            continue;
        }

        fs::path relative_path = fs::relative(full_path, vault_path);

        auto meta = scan_file(vault_path, relative_path);

        if (meta.has_value()) {
            files.push_back(*meta);
        }
    }

    return files;
}
