#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

// Content-addressed storage for a synced text file's base content, keyed by
// its SHA-256 hash. The three-way text merge needs the actual base content
// (not just its hash) to diff against; this is where it lives between runs,
// separate from the vault itself so it never gets synced or scanned.
class ObjectStore {
public:
    explicit ObjectStore(std::filesystem::path objects_dir);

    // Stores `content` under its hash if not already present, and returns
    // that hash.
    std::string put(const std::string &content) const;

    std::optional<std::string> get(const std::string &hash) const;

    bool has(const std::string &hash) const;

    // Removes every stored object whose hash is not in `keep_hashes`, e.g.
    // once a file's base state has moved on and its old content is no
    // longer needed for a future merge.
    void prune(const std::unordered_set<std::string> &keep_hashes) const;

private:
    std::filesystem::path objects_dir_;
};
