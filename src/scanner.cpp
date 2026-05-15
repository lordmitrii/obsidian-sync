#include "scanner.hpp"
#include "hasher.hpp"

#include <chrono>

namespace fs = std::filesystem;

bool should_ignore(const fs::path &path) {
    std::string s = path.string();

    if (s.find(".DS_Store") != std::string::npos)
        return true;
    if (s.find(".obsidian/workspace.json") != std::string::npos)
        return true;
    if (s.find(".obsidian/workspace-mobile.json") != std::string::npos)
        return true;
    if (s.find(".obsidian/cache") != std::string::npos)
        return true;
    if (s.find(".trash") != std::string::npos)
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

        FileMeta meta;
        meta.path = relative_path.generic_string();
        meta.size = fs::file_size(full_path);
        meta.modified_time = get_modified_time(full_path);
        meta.hash = sha256_file(full_path);

        files.push_back(meta);
    }

    return files;
}