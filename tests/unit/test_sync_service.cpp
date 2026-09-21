#include "local_remote_backend.hpp"
#include "sync_service.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static void write_file(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

static std::string read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Fixture {
    fs::path root;
    fs::path local;
    fs::path remote;
    std::string state;

    explicit Fixture(const std::string &name)
        : root(fs::temp_directory_path() / ("obsidian-sync-service-test-" + name)),
          local(root / "local"),
          remote(root / "remote"),
          state((root / "state.db").string()) {
        fs::remove_all(root);
        fs::create_directories(local);
        fs::create_directories(remote);
    }

    ~Fixture() { fs::remove_all(root); }

    void sync() {
        LocalRemoteBackend backend(remote);
        SyncService service(local, state, backend, true);
        service.run_once();
    }
};

static void test_first_sync_uploads_and_downloads() {
    Fixture f("first");
    write_file(f.local / "mine.md", "local note");
    write_file(f.remote / "theirs.md", "remote note");

    f.sync();

    expect(read_file(f.remote / "mine.md") == "local note", "first_sync_uploads_local_file");
    expect(read_file(f.local / "theirs.md") == "remote note", "first_sync_downloads_remote_file");
}

static void test_dry_run_changes_nothing() {
    Fixture f("dry");
    write_file(f.local / "mine.md", "local note");

    LocalRemoteBackend backend(f.remote);
    SyncService service(f.local, f.state, backend, false);
    service.run_once();

    expect(!fs::exists(f.remote / "mine.md"), "dry_run_does_not_upload");
}

static void test_local_edit_propagates_after_base_recorded() {
    Fixture f("edit");
    write_file(f.local / "a.md", "v1");
    f.sync();

    write_file(f.local / "a.md", "v2 with more text");
    f.sync();

    expect(read_file(f.remote / "a.md") == "v2 with more text", "local_edit_uploaded");
}

static void test_local_delete_propagates() {
    Fixture f("delete");
    write_file(f.local / "a.md", "v1");
    f.sync();

    fs::remove(f.local / "a.md");
    f.sync();

    expect(!fs::exists(f.remote / "a.md"), "local_delete_removes_remote");
}

static void test_remote_delete_propagates() {
    Fixture f("remote-delete");
    write_file(f.local / "a.md", "v1");
    f.sync();

    fs::remove(f.remote / "a.md");
    f.sync();

    expect(!fs::exists(f.local / "a.md"), "remote_delete_removes_local");
}

static void test_conflict_keeps_local_and_saves_remote_copy() {
    Fixture f("conflict");
    write_file(f.local / "a.md", "base");
    f.sync();

    write_file(f.local / "a.md", "local edit");
    write_file(f.remote / "a.md", "remote edit!");
    f.sync();

    expect(read_file(f.local / "a.md") == "local edit", "conflict_keeps_local");
    expect(read_file(f.remote / "a.md") == "remote edit!", "conflict_keeps_remote");
    expect(read_file(f.local / "a.conflict-remote.md") == "remote edit!",
           "conflict_saves_remote_copy");
}

int main() {
    test_first_sync_uploads_and_downloads();
    test_dry_run_changes_nothing();
    test_local_edit_propagates_after_base_recorded();
    test_local_delete_propagates();
    test_remote_delete_propagates();
    test_conflict_keeps_local_and_saves_remote_copy();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "sync service tests passed\n";
    return 0;
}
