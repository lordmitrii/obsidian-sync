#include "db.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static FileMeta meta(const std::string &path, const std::string &hash, std::uintmax_t size = 1) {
    return FileMeta{path, hash, size, 100};
}

static fs::path fresh_db_path(const std::string &name) {
    const auto path = fs::temp_directory_path() / ("obsidian-sync-db-test-" + name + ".db");
    fs::remove(path);
    return path;
}

static void test_save_and_get() {
    const auto path = fresh_db_path("save-get");
    {
        Database db(path.string());
        db.initialize();
        db.save_file(FileMeta{"notes/a.md", "abc", 42, 1700000000});

        const auto file = db.get_file("notes/a.md");
        expect(file.has_value(), "saved_file_found");
        expect(file && file->hash == "abc", "saved_hash");
        expect(file && file->size == 42, "saved_size");
        expect(file && file->modified_time == 1700000000, "saved_mtime");
        expect(!db.get_file("missing.md").has_value(), "missing_file_is_nullopt");
    }
    fs::remove(path);
}

static void test_save_overwrites_existing_row() {
    const auto path = fresh_db_path("upsert");
    {
        Database db(path.string());
        db.initialize();
        db.save_file(meta("a.md", "old"));
        db.save_file(meta("a.md", "new", 7));

        expect(db.get_all_paths().size() == 1, "upsert_keeps_one_row");
        const auto file = db.get_file("a.md");
        expect(file && file->hash == "new" && file->size == 7, "upsert_replaces_values");
    }
    fs::remove(path);
}

static void test_delete_and_list() {
    const auto path = fresh_db_path("delete");
    {
        Database db(path.string());
        db.initialize();
        db.save_file(meta("a.md", "1"));
        db.save_file(meta("b.md", "2"));
        db.delete_file("a.md");
        db.delete_file("never-existed.md");

        auto paths = db.get_all_paths();
        expect(paths.size() == 1 && paths[0] == "b.md", "delete_removes_only_target");

        const auto manifest = db.load_as_manifest();
        expect(manifest.size() == 1 && manifest.count("b.md") == 1, "manifest_matches_rows");
    }
    fs::remove(path);
}

static void test_state_survives_reopen() {
    const auto path = fresh_db_path("reopen");
    {
        Database db(path.string());
        db.initialize();
        db.save_file(meta("a.md", "kept"));
    }
    {
        Database db(path.string());
        db.initialize();
        const auto file = db.get_file("a.md");
        expect(file && file->hash == "kept", "reopen_keeps_rows");
    }
    fs::remove(path);
}

static void test_initialize_is_safe_to_call_repeatedly() {
    const auto path = fresh_db_path("reinitialize");
    Database db(path.string());
    db.initialize();
    db.initialize();
    db.save_file(meta("a.md", "x"));
    expect(db.get_file("a.md").has_value(), "repeated_initialize_keeps_files_table_usable");
    fs::remove(path);
}

static void test_record_and_resolve_conflict() {
    const auto path = fresh_db_path("conflict-lifecycle");
    Database db(path.string());
    db.initialize();
    db.record_conflict("a.md", "local-hash", "remote-hash", 1000);

    auto unresolved = db.get_unresolved_conflicts();
    expect(unresolved.size() == 1, "conflict_recorded");
    expect(unresolved.size() == 1 && unresolved[0].path == "a.md", "conflict_path");
    expect(unresolved.size() == 1 && unresolved[0].local_hash == "local-hash",
          "conflict_local_hash");
    expect(unresolved.size() == 1 && unresolved[0].remote_hash == "remote-hash",
          "conflict_remote_hash");
    expect(unresolved.size() == 1 && unresolved[0].detected_time == 1000,
          "conflict_detected_time");

    db.mark_conflict_resolved("a.md", "remote", 2000);
    expect(db.get_unresolved_conflicts().empty(), "resolved_conflict_no_longer_unresolved");
    fs::remove(path);
}

static void test_record_conflict_upserts_on_retrigger() {
    const auto path = fresh_db_path("conflict-upsert");
    Database db(path.string());
    db.initialize();
    db.record_conflict("a.md", "local-1", "remote-1", 1000);
    db.record_conflict("a.md", "local-2", "remote-2", 2000);

    auto unresolved = db.get_unresolved_conflicts();
    expect(unresolved.size() == 1, "retriggered_conflict_keeps_one_row");
    expect(unresolved.size() == 1 && unresolved[0].remote_hash == "remote-2",
          "retriggered_conflict_updates_hash");
    fs::remove(path);
}

int main() {
    test_save_and_get();
    test_save_overwrites_existing_row();
    test_delete_and_list();
    test_state_survives_reopen();
    test_initialize_is_safe_to_call_repeatedly();
    test_record_and_resolve_conflict();
    test_record_conflict_upserts_on_retrigger();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All db tests passed\n";
    return 0;
}
