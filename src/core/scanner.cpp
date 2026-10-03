#include "scanner.hpp"
#include "hasher.hpp"

#include <chrono>

namespace fs = std::filesystem;

// A cached hash is trusted only when the file's mtime is at least a second
// old: a file written and then rewritten within the same mtime tick would
// otherwise keep matching a now-stale cached hash forever, the same race
// git's index guards against.
static bool is_racy_mtime(std::int64_t modified_time) {
    // Compared using the filesystem clock, not system_clock: their epochs
    // are not guaranteed to line up, and get_modified_time() below already
    // reads mtime through the filesystem clock.
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   fs::file_time_type::clock::now().time_since_epoch())
                   .count();

    return now - modified_time <= 1;
}

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

    const std::string filename = path.filename().string();
    if (filename.find(".conflict-remote.") != std::string::npos ||
        filename.ends_with(".conflict-remote"))
        return true;

    if (path.extension() == ".tmp")
        return true;
    if (path.extension() == ".swp")
        return true;
    if (path.extension() == ".crswap")
        return true;

    return false;
}

bool should_ignore(const fs::path &path, const fs::path &relative_path,
                   const IgnoreRules &extra_rules) {
    if (should_ignore(path))
        return true;

    return extra_rules.matches(relative_path);
}

std::int64_t get_modified_time(const fs::path &path) {
    auto time = fs::last_write_time(path);

    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch());

    return seconds.count();
}

std::optional<FileMeta> scan_file(const fs::path &root, const fs::path &relative_path,
                                  const Manifest &known_hashes) {
    fs::path full_path = root / relative_path;

    if (fs::is_symlink(full_path)) {
        return std::nullopt;
    }

    if (!fs::exists(full_path) || !fs::is_regular_file(full_path) || should_ignore(full_path)) {
        return std::nullopt;
    }

    FileMeta meta;
    meta.path = relative_path.generic_string();
    meta.size = fs::file_size(full_path);
    meta.modified_time = get_modified_time(full_path);

    auto cached = known_hashes.find(meta.path);
    bool cache_hit = cached != known_hashes.end() && cached->second.size == meta.size &&
                     cached->second.modified_time == meta.modified_time &&
                     !is_racy_mtime(meta.modified_time);

    meta.hash = cache_hit ? cached->second.hash : sha256_file(full_path);

    return meta;
}

std::vector<FileMeta> scan_vault(const fs::path &vault_path, const Manifest &known_hashes,
                                 SyncConfigPreset sync_config) {
    std::vector<FileMeta> files;
    IgnoreRules extra_rules = load_ignore_rules(vault_path);
    extra_rules.merge(ignore_rules_for_sync_config(sync_config));

    for (const auto &entry : fs::recursive_directory_iterator(vault_path)) {
        if (entry.is_symlink() || !entry.is_regular_file()) {
            continue;
        }

        fs::path full_path = entry.path();
        fs::path relative_path = fs::relative(full_path, vault_path);

        if (should_ignore(full_path, relative_path, extra_rules)) {
            continue;
        }

        auto meta = scan_file(vault_path, relative_path, known_hashes);

        if (meta.has_value()) {
            files.push_back(*meta);
        }
    }

    return files;
}
