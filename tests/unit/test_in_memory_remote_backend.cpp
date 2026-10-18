#include "hasher.hpp"
#include "in_memory_remote_backend.hpp"
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

static void test_upload_then_manifest_reflects_content() {
    InMemoryRemoteBackend backend;
    const auto root = fs::temp_directory_path() / "obsidian-sync-inmem-test-upload";
    write_file(root / "local.md", "hello");

    backend.upload("note.md", root / "local.md");
    const auto manifest = backend.load_manifest();

    expect(manifest.count("note.md") == 1, "manifest_has_uploaded_path");
    expect(manifest.at("note.md").hash == sha256_bytes("hello"), "manifest_hash_matches_content");
    expect(manifest.at("note.md").size == 5, "manifest_size_matches_content");
    fs::remove_all(root);
}

static void test_download_roundtrip() {
    InMemoryRemoteBackend backend;
    backend.set_content("note.md", "remote content");
    const auto root = fs::temp_directory_path() / "obsidian-sync-inmem-test-download";

    backend.download("note.md", root / "sub/local.md");

    expect(read_file(root / "sub/local.md") == "remote content", "download_writes_content");
    fs::remove_all(root);
}

static void test_delete_remote() {
    InMemoryRemoteBackend backend;
    backend.set_content("note.md", "x");

    backend.delete_remote("note.md");

    expect(backend.load_manifest().empty(), "delete_removes_from_manifest");
}

static void test_conflict_copy_is_saved_without_overwriting() {
    InMemoryRemoteBackend backend;
    backend.set_content("note.md", "remote version");
    const auto root = fs::temp_directory_path() / "obsidian-sync-inmem-test-conflict";
    const auto copy_path = root / "note.conflict-remote.md";

    backend.save_conflict_copy("note.md", copy_path);
    expect(read_file(copy_path) == "remote version", "conflict_copy_written");

    backend.set_content("note.md", "changed again");
    bool threw = false;

    try {
        backend.save_conflict_copy("note.md", copy_path);
    } catch (const fs::filesystem_error &) {
        threw = true;
    }

    expect(threw, "existing_conflict_copy_is_not_overwritten");
    fs::remove_all(root);
}

static void test_conflict_copy_skipped_when_remote_missing() {
    InMemoryRemoteBackend backend;
    const auto root = fs::temp_directory_path() / "obsidian-sync-inmem-test-conflict-missing";
    const auto copy_path = root / "note.conflict-remote.md";

    backend.save_conflict_copy("note.md", copy_path);

    expect(!fs::exists(copy_path), "no_copy_for_missing_remote");
}

// The actual point of InMemoryRemoteBackend: SyncService exercised end to
// end with no second real directory standing in for "remote", and no HTTP
// server either.
struct SyncFixture {
    fs::path local;
    std::string state;
    InMemoryRemoteBackend backend;

    explicit SyncFixture(const std::string &name)
        : local(fs::temp_directory_path() / ("obsidian-sync-inmem-service-test-" + name)),
          state((local.parent_path() / (name + "-state.db")).string()) {
        fs::remove_all(local);
        fs::create_directories(local);
        fs::remove(state);
    }

    ~SyncFixture() {
        fs::remove_all(local);
        fs::remove(state);
    }

    RunStatus sync() {
        SyncService service(local, state, backend, true);
        return service.run_once();
    }
};

static void test_first_sync_uploads_and_downloads() {
    SyncFixture f("first");
    write_file(f.local / "mine.md", "local note");
    f.backend.set_content("theirs.md", "remote note");

    expect(f.sync() == RunStatus::Success, "first_sync_succeeds");
    expect(f.backend.load_manifest().at("mine.md").hash == sha256_bytes("local note"),
           "first_sync_uploads_local_file");
    expect(read_file(f.local / "theirs.md") == "remote note", "first_sync_downloads_remote_file");
}

static void test_conflicting_edit_is_detected_and_kept() {
    SyncFixture f("conflict");
    write_file(f.local / "note.md", "base");
    f.backend.set_content("note.md", "base");
    f.sync();

    write_file(f.local / "note.md", "local edit");
    f.backend.set_content("note.md", "remote edit");

    expect(f.sync() == RunStatus::ConflictsPending, "conflicting_edit_reports_pending");
    expect(read_file(f.local / "note.md") == "local edit", "conflict_keeps_local_content");
    expect(read_file(f.local / "note.conflict-remote.md") == "remote edit",
           "conflict_saves_remote_copy");
}

static void test_local_delete_propagates_to_remote() {
    SyncFixture f("delete");
    write_file(f.local / "note.md", "content");
    f.sync();

    fs::remove(f.local / "note.md");

    expect(f.sync() == RunStatus::Success, "delete_sync_succeeds");
    expect(f.backend.load_manifest().count("note.md") == 0, "delete_propagates_to_remote");
}

int main() {
    test_upload_then_manifest_reflects_content();
    test_download_roundtrip();
    test_delete_remote();
    test_conflict_copy_is_saved_without_overwriting();
    test_conflict_copy_skipped_when_remote_missing();
    test_first_sync_uploads_and_downloads();
    test_conflicting_edit_is_detected_and_kept();
    test_local_delete_propagates_to_remote();

    if (failures != 0) {
        std::cerr << failures << " test case(s) failed\n";
        return 1;
    }

    std::cout << "All in-memory remote backend tests passed\n";
    return 0;
}
