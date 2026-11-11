#pragma once

#include "object_store.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct FileVersion {
    std::string hash;
    std::string timestamp;
    std::size_t size = 0;
};

class VersionHistory {
public:
    explicit VersionHistory(std::filesystem::path versions_root);

    void record(const std::filesystem::path &relative_path, const std::string &content) const;

    std::vector<FileVersion> list(const std::filesystem::path &relative_path) const;

    std::optional<std::string> content(const std::string &hash) const;

    void prune_all(std::size_t max_versions_per_file, int retention_days) const;

private:
    std::filesystem::path versions_root_;
    ObjectStore objects_;

    std::filesystem::path index_path(const std::filesystem::path &relative_path) const;
};
