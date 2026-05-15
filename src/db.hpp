#pragma once

#include "filemeta.hpp"
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

class Database {
  public:
    Database(const std::string &path);
    ~Database();

    void initialize();
    void save_file(const FileMeta &file);
    std::optional<FileMeta> get_file(const std::string &path);
    std::vector<std::string> get_all_paths();
    void delete_file(const std::string &path);

  private:
    sqlite3 *db;
};
