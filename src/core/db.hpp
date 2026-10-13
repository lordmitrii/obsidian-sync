#pragma once

#include "filemeta.hpp"
#include "manifest.hpp"

#include <cstdint>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

struct ConflictRecord {
    std::string path;
    std::string local_hash;
    std::string remote_hash;
    std::int64_t detected_time;
};

class Database {
  public:
    Database(const std::string &path);
    ~Database();

    void initialize();
    void save_file(const FileMeta &file);
    std::optional<FileMeta> get_file(const std::string &path);
    std::vector<std::string> get_all_paths();
    Manifest load_as_manifest();
    void delete_file(const std::string &path);

    void record_conflict(const std::string &path, const std::string &local_hash,
                         const std::string &remote_hash, std::int64_t detected_time);
    void mark_conflict_resolved(const std::string &path, const std::string &resolution,
                                std::int64_t resolved_time);
    std::vector<ConflictRecord> get_unresolved_conflicts();

  private:
    sqlite3 *db;
};
