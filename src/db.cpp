#include "db.hpp"

#include <iostream>
#include <stdexcept>
#include <optional>
#include <vector>

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        throw std::runtime_error("Failed to open database");
    }
}

Database::~Database() {
    sqlite3_close(db);
}

void Database::initialize() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS files ("
        "path TEXT PRIMARY KEY,"
        "hash TEXT NOT NULL,"
        "size INTEGER NOT NULL,"
        "modified_time INTEGER NOT NULL"
        ");";

    char* error_message = nullptr;

    if (sqlite3_exec(db, sql, nullptr, nullptr, &error_message) != SQLITE_OK) {
        std::string error = error_message;

        sqlite3_free(error_message);

        throw std::runtime_error(error);
    }
}

void Database::save_file(const FileMeta& file) {
    const char* sql =
        "INSERT INTO files (path, hash, size, modified_time) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "hash = excluded.hash, "
        "size = excluded.size, "
        "modified_time = excluded.modified_time;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(stmt, 1, file.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file.hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(file.size));
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(file.modified_time));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
}

std::optional<FileMeta> Database::get_file(const std::string& path) {
    const char* sql =
        "SELECT path, hash, size, modified_time "
        "FROM files "
        "WHERE path = ?;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    int result = sqlite3_step(stmt);

    if (result == SQLITE_ROW) {
        FileMeta file;
        file.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        file.hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        file.size = static_cast<std::uintmax_t>(sqlite3_column_int64(stmt, 2));
        file.modified_time = sqlite3_column_int64(stmt, 3);

        sqlite3_finalize(stmt);
        return file;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<std::string> Database::get_all_paths() {
    const char* sql = "SELECT path FROM files;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    std::vector<std::string> paths;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        paths.emplace_back(reinterpret_cast<const char*>(text));
    }

    sqlite3_finalize(stmt);
    return paths;
}


void Database::delete_file(const std::string& path) {
    const char* sql = "DELETE FROM files WHERE path = ?;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
}