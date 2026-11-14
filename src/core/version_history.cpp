#include "version_history.hpp"

#include "atomic_file.hpp"
#include "time_utils.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

fs::path index_root(const fs::path &versions_root) { return versions_root / "index"; }

fs::path objects_root(const fs::path &versions_root) { return versions_root / "objects"; }

std::vector<FileVersion> read_index(const fs::path &index_file) {
    std::vector<FileVersion> entries;
    std::ifstream in(index_file);

    if (!in) {
        return entries;
    }

    nlohmann::json j;
    in >> j;

    for (const auto &item : j) {
        FileVersion entry;
        entry.hash = item.at("hash").get<std::string>();
        entry.timestamp = item.at("timestamp").get<std::string>();
        entry.size = item.at("size").get<std::size_t>();
        entries.push_back(std::move(entry));
    }

    return entries;
}

void write_index(const fs::path &index_file, const std::vector<FileVersion> &entries) {
    nlohmann::json j = nlohmann::json::array();

    for (const auto &entry : entries) {
        j.push_back({{"hash", entry.hash}, {"timestamp", entry.timestamp}, {"size", entry.size}});
    }

    atomic_write_file(index_file, j.dump(2));
}

} // namespace

VersionHistory::VersionHistory(fs::path versions_root)
    : versions_root_(std::move(versions_root)), objects_(objects_root(versions_root_)) {}

fs::path VersionHistory::index_path(const fs::path &relative_path) const {
    return index_root(versions_root_) / (relative_path.string() + ".json");
}

void VersionHistory::record(const fs::path &relative_path, const std::string &content) const {
    std::string hash = objects_.put(content);
    std::vector<FileVersion> entries = read_index(index_path(relative_path));

    if (!entries.empty() && entries.back().hash == hash) {
        return;
    }

    FileVersion entry;
    entry.hash = hash;
    entry.timestamp = current_timestamp_iso8601();
    entry.size = content.size();
    entries.push_back(entry);

    write_index(index_path(relative_path), entries);
}

std::vector<FileVersion> VersionHistory::list(const fs::path &relative_path) const {
    std::vector<FileVersion> entries = read_index(index_path(relative_path));
    std::reverse(entries.begin(), entries.end());
    return entries;
}

std::optional<std::string> VersionHistory::content(const std::string &hash) const {
    return objects_.get(hash);
}

void VersionHistory::prune_all(std::size_t max_versions_per_file, int retention_days) const {
    fs::path index_dir = index_root(versions_root_);

    if (!fs::exists(index_dir)) {
        return;
    }

    std::string cutoff;

    if (retention_days > 0) {
        cutoff = format_timestamp_iso8601(std::chrono::system_clock::now() -
                                          std::chrono::hours(24 * retention_days));
    }

    std::unordered_set<std::string> keep_hashes;

    for (const auto &entry : fs::recursive_directory_iterator(index_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::vector<FileVersion> entries = read_index(entry.path());

        if (!cutoff.empty()) {
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                               [&cutoff](const FileVersion &v) { return v.timestamp < cutoff; }),
                entries.end());
        }

        if (max_versions_per_file > 0 && entries.size() > max_versions_per_file) {
            entries.erase(entries.begin(),
                          entries.end() - static_cast<std::ptrdiff_t>(max_versions_per_file));
        }

        if (entries.empty()) {
            std::error_code ec;
            fs::remove(entry.path(), ec);
            continue;
        }

        write_index(entry.path(), entries);

        for (const auto &version : entries) {
            keep_hashes.insert(version.hash);
        }
    }

    objects_.prune(keep_hashes);
}
