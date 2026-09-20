#include "local_remote_backend.hpp"

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

static fs::path fresh_dir(const std::string &name) {
    const auto dir = fs::temp_directory_path() / ("obsidian-sync-backend-test-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static void test_upload_creates_parent_directories() {
    const auto root = fresh_dir("upload");
    const auto remote = root / "remote";
    write_file(root / "local.md", "hello");

    LocalRemoteBackend backend(remote);
    backend.upload("deep/nested/note.md", root / "local.md");

    expect(read_file(remote / "deep/nested/note.md") == "hello", "upload_writes_content");
    fs::remove_all(root);
}

static void test_upload_overwrites_and_download_roundtrips() {
    const auto root = fresh_dir("roundtrip");
    const auto remote = root / "remote";
    write_file(remote / "a.md", "old");
    write_file(root / "local.md", "new");

    LocalRemoteBackend backend(remote);
    backend.upload("a.md", root / "local.md");
    expect(read_file(remote / "a.md") == "new", "upload_overwrites_existing");

    backend.download("a.md", root / "vault/sub/a.md");
    expect(read_file(root / "vault/sub/a.md") == "new", "download_writes_content");
    fs::remove_all(root);
}

static void test_delete_remote() {
    const auto root = fresh_dir("delete");
    const auto remote = root / "remote";
    write_file(remote / "a.md", "x");

    LocalRemoteBackend backend(remote);
    backend.delete_remote("a.md");

    expect(!fs::exists(remote / "a.md"), "delete_removes_file");
    fs::remove_all(root);
}

static void test_manifest_lists_remote_files() {
    const auto root = fresh_dir("manifest");
    const auto remote = root / "remote";
    write_file(remote / "a.md", "one");
    write_file(remote / "dir/b.md", "two");
    write_file(remote / ".obsidian/workspace.json", "{}");

    LocalRemoteBackend backend(remote);
    const auto manifest = backend.load_manifest();

    expect(manifest.size() == 2, "manifest_skips_ignored_files");
    expect(manifest.count("a.md") == 1, "manifest_has_top_level_file");
    expect(manifest.count("dir/b.md") == 1, "manifest_has_nested_file");
    fs::remove_all(root);
}

static void test_conflict_copy_is_saved_without_overwriting() {
    const auto root = fresh_dir("conflict");
    const auto remote = root / "remote";
    write_file(remote / "a.md", "remote version");

    LocalRemoteBackend backend(remote);
    const auto copy_path = root / "vault/a.conflict-remote.md";

    backend.save_conflict_copy("a.md", copy_path);
    expect(read_file(copy_path) == "remote version", "conflict_copy_written");

    write_file(remote / "a.md", "changed again");
    bool threw = false;
    try {
        backend.save_conflict_copy("a.md", copy_path);
    } catch (const fs::filesystem_error &) {
        threw = true;
    }
    expect(threw, "existing_conflict_copy_is_not_overwritten");
    expect(read_file(copy_path) == "remote version", "existing_conflict_copy_untouched");
    fs::remove_all(root);
}

static void test_conflict_copy_skipped_when_remote_missing() {
    const auto root = fresh_dir("conflict-missing");
    LocalRemoteBackend backend(root / "remote");
    const auto copy_path = root / "vault/a.conflict-remote.md";

    backend.save_conflict_copy("a.md", copy_path);

    expect(!fs::exists(copy_path), "no_copy_for_missing_remote");
    fs::remove_all(root);
}

int main() {
    test_upload_creates_parent_directories();
    test_upload_overwrites_and_download_roundtrips();
    test_delete_remote();
    test_manifest_lists_remote_files();
    test_conflict_copy_is_saved_without_overwriting();
    test_conflict_copy_skipped_when_remote_missing();

    if (failures != 0) {
        std::cerr << failures << " test case(s) failed\n";
        return 1;
    }

    std::cout << "All local backend tests passed\n";
    return 0;
}
