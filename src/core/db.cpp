#include "db.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

Database::Database(const std::string &path) {
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        throw std::runtime_error("Failed to open database");
    }
}

Database::~Database() { sqlite3_close(db); }

static void run_sql(sqlite3 *db, const std::string &sql) {
    char *error_message = nullptr;

    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message) != SQLITE_OK) {
        std::string error = error_message;

        sqlite3_free(error_message);

        throw std::runtime_error(error);
    }
}

static int schema_version(sqlite3 *db) {
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    int version = 0;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return version;
}

// Each entry migrates the schema from its own index to index+1; applied in
// order starting from the db's current PRAGMA user_version, so an existing
// db only runs the steps it hasn't seen yet.
void Database::initialize() {
    static const std::vector<std::string> migrations = {
        "CREATE TABLE IF NOT EXISTS files ("
        "path TEXT PRIMARY KEY,"
        "hash TEXT NOT NULL,"
        "size INTEGER NOT NULL,"
        "modified_time INTEGER NOT NULL"
        ");",
        "CREATE TABLE IF NOT EXISTS conflicts ("
        "path TEXT PRIMARY KEY,"
        "local_hash TEXT NOT NULL,"
        "remote_hash TEXT NOT NULL,"
        "detected_time INTEGER NOT NULL,"
        "resolution TEXT,"
        "resolved_time INTEGER"
        ");",
    };

    for (size_t version = static_cast<size_t>(schema_version(db)); version < migrations.size();
         ++version) {
        run_sql(db, migrations[version]);
        run_sql(db, "PRAGMA user_version = " + std::to_string(version + 1) + ";");
    }
}

void Database::record_conflict(const std::string &path, const std::string &local_hash,
                               const std::string &remote_hash, std::int64_t detected_time) {
    const char *sql = "INSERT INTO conflicts (path, local_hash, remote_hash, detected_time, "
                      "resolution, resolved_time) "
                      "VALUES (?, ?, ?, ?, NULL, NULL) "
                      "ON CONFLICT(path) DO UPDATE SET "
                      "local_hash = excluded.local_hash, "
                      "remote_hash = excluded.remote_hash, "
                      "detected_time = excluded.detected_time, "
                      "resolution = NULL, "
                      "resolved_time = NULL;";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, local_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, remote_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(detected_time));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
}

void Database::mark_conflict_resolved(const std::string &path, const std::string &resolution,
                                      std::int64_t resolved_time) {
    const char *sql = "UPDATE conflicts SET resolution = ?, resolved_time = ? WHERE path = ?;";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(stmt, 1, resolution.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(resolved_time));
    sqlite3_bind_text(stmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
}

std::vector<ConflictRecord> Database::get_unresolved_conflicts() {
    const char *sql = "SELECT path, local_hash, remote_hash, detected_time FROM conflicts "
                      "WHERE resolution IS NULL;";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    std::vector<ConflictRecord> records;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ConflictRecord record;
        record.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        record.local_hash = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        record.remote_hash = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        record.detected_time = sqlite3_column_int64(stmt, 3);
        records.push_back(std::move(record));
    }

    sqlite3_finalize(stmt);
    return records;
}

void Database::save_file(const FileMeta &file) {
    const char *sql = "INSERT INTO files (path, hash, size, modified_time) "
                      "VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(path) DO UPDATE SET "
                      "hash = excluded.hash, "
                      "size = excluded.size, "
                      "modified_time = excluded.modified_time;";

    sqlite3_stmt *stmt = nullptr;

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

std::optional<FileMeta> Database::get_file(const std::string &path) {
    const char *sql = "SELECT path, hash, size, modified_time "
                      "FROM files "
                      "WHERE path = ?;";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    int result = sqlite3_step(stmt);

    if (result == SQLITE_ROW) {
        FileMeta file;
        file.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        file.hash = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        file.size = static_cast<std::uintmax_t>(sqlite3_column_int64(stmt, 2));
        file.modified_time = sqlite3_column_int64(stmt, 3);

        sqlite3_finalize(stmt);
        return file;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<std::string> Database::get_all_paths() {
    const char *sql = "SELECT path FROM files;";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    std::vector<std::string> paths;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        paths.emplace_back(reinterpret_cast<const char *>(text));
    }

    sqlite3_finalize(stmt);
    return paths;
}

Manifest Database::load_as_manifest() {
    const char *sql = "SELECT path, hash, size, modified_time FROM files;";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    Manifest manifest;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileMeta file;
        file.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        file.hash = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        file.size = static_cast<std::uintmax_t>(sqlite3_column_int64(stmt, 2));
        file.modified_time = sqlite3_column_int64(stmt, 3);

        manifest[file.path] = file;
    }

    sqlite3_finalize(stmt);
    return manifest;
}

void Database::delete_file(const std::string &path) {
    const char *sql = "DELETE FROM files WHERE path = ?;";

    sqlite3_stmt *stmt = nullptr;

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
