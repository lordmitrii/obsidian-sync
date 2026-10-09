#include "vault_status.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

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

static fs::path fresh_dir(const std::string &name) {
    const auto dir = fs::temp_directory_path() / ("obsidian-sync-status-test-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static void test_counts_tracked_files_from_manifest() {
    Manifest manifest;
    manifest["a.md"] = {"a.md", "hash-a", 10, 1};
    manifest["b.md"] = {"b.md", "hash-b", 20, 2};

    const auto root = fresh_dir("tracked");
    const VaultStatus status = compute_vault_status(manifest, root);

    expect(status.tracked_files == 2, "tracked_files");
    expect(status.tracked_bytes == 30, "tracked_bytes");
    expect(status.unresolved_conflicts == 0, "no_conflicts");
    expect(status.trashed_files == 0, "no_trashed_files");
}

static void test_counts_conflicts_and_trash() {
    const auto root = fresh_dir("conflicts-and-trash");
    write_file(root / "note.md", "content");
    write_file(root / "note.conflict-remote.md", "remote copy");
    write_file(root / "note.conflict-remote.2.md", "second remote copy");
    write_file(root / ".trash" / "deleted.md", "gone");

    const VaultStatus status = compute_vault_status({}, root);

    expect(status.unresolved_conflicts == 2, "counts_both_conflict_copies");
    expect(status.trashed_files == 1, "counts_trashed_file");
}

static void test_lists_conflicted_notes_deduplicated() {
    const auto root = fresh_dir("list-conflicts");
    write_file(root / "note.md", "content");
    write_file(root / "note.conflict-remote.md", "remote copy");
    write_file(root / "note.conflict-remote.2.md", "second remote copy");
    write_file(root / "folder" / "other.conflict-remote", "extensionless");
    write_file(root / "folder" / "other.2.conflict-remote", "second extensionless copy");
    write_file(root / ".trash" / "trashed.conflict-remote.md", "ignored");

    const auto notes = list_conflicted_notes(root);

    expect(notes.size() == 2, "dedupes_numbered_copies");
    expect(notes[0] == "folder/other", "lists_nested_note_without_extension");
    expect(notes[1] == "note.md", "lists_note_with_extension");
}

static void test_list_conflicted_notes_empty_when_none() {
    const auto root = fresh_dir("list-conflicts-empty");
    write_file(root / "note.md", "content");

    expect(list_conflicted_notes(root).empty(), "no_conflicts_found");
}

static void test_missing_vault_path_returns_zero_counts() {
    const VaultStatus status = compute_vault_status({}, fs::temp_directory_path() / "does-not-exist");

    expect(status.unresolved_conflicts == 0, "missing_vault_no_conflicts");
    expect(status.trashed_files == 0, "missing_vault_no_trashed_files");
}

int main() {
    test_counts_tracked_files_from_manifest();
    test_counts_conflicts_and_trash();
    test_lists_conflicted_notes_deduplicated();
    test_list_conflicted_notes_empty_when_none();
    test_missing_vault_path_returns_zero_counts();

    if (failures == 0) {
        std::cout << "All vault status tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
