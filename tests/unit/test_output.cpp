#include "manifest_action.hpp"
#include "output.hpp"

#include <iostream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static void test_manifest_to_json() {
    const auto empty = manifest_to_json({});
    expect(empty["files"].is_array() && empty["files"].empty(), "empty_manifest_has_empty_array");

    const auto root = manifest_to_json({{"notes/a.md", "abc", 12, 1700000000}});
    expect(root["files"].size() == 1, "one_entry");
    expect(root["files"][0]["path"] == "notes/a.md", "path_field");
    expect(root["files"][0]["hash"] == "abc", "hash_field");
    expect(root["files"][0]["size"] == 12, "size_field");
    expect(root["files"][0]["modified_time"] == 1700000000, "modified_time_field");
}

static void test_manifest_to_json_keeps_order() {
    const auto root = manifest_to_json({{"b.md", "h1", 1, 1}, {"a.md", "h2", 2, 2}});
    expect(root["files"][0]["path"] == "b.md" && root["files"][1]["path"] == "a.md",
           "entries_keep_input_order");
}

static void test_vault_status_to_json() {
    VaultStatus status;
    status.tracked_files = 3;
    status.tracked_bytes = 42;
    status.unresolved_conflicts = 1;
    status.trashed_files = 2;

    const auto root = vault_status_to_json(status);

    expect(root["tracked_files"] == 3, "tracked_files_field");
    expect(root["tracked_bytes"] == 42, "tracked_bytes_field");
    expect(root["unresolved_conflicts"] == 1, "unresolved_conflicts_field");
    expect(root["trashed_files"] == 2, "trashed_files_field");
}

static void test_conflicted_notes_to_json() {
    const auto empty = conflicted_notes_to_json({});
    expect(empty["conflicts"].is_array() && empty["conflicts"].empty(), "empty_conflicts_array");

    const auto root = conflicted_notes_to_json(
        {{"note.md", "abc", "def", 1700000000}, {"folder/other.md", "", "", 0}});
    expect(root["conflicts"].size() == 2, "two_conflicts");
    expect(root["conflicts"][0]["path"] == "note.md", "first_conflict_path");
    expect(root["conflicts"][0]["local_hash"] == "abc", "first_conflict_local_hash");
    expect(root["conflicts"][0]["remote_hash"] == "def", "first_conflict_remote_hash");
    expect(root["conflicts"][0]["detected_time"] == 1700000000, "first_conflict_detected_time");
    expect(root["conflicts"][1]["path"] == "folder/other.md", "second_conflict_path");
    expect(root["conflicts"][1]["local_hash"] == "", "second_conflict_has_no_recorded_hash");
}

static void test_file_versions_to_json() {
    const auto empty = file_versions_to_json({});
    expect(empty["versions"].is_array() && empty["versions"].empty(), "empty_versions_array");

    const auto root = file_versions_to_json(
        {{"hash1", "2026-01-01T00:00:00Z", 10}, {"hash2", "2026-01-02T00:00:00Z", 20}});
    expect(root["versions"].size() == 2, "two_versions");
    expect(root["versions"][0]["hash"] == "hash1", "first_version_hash");
    expect(root["versions"][0]["timestamp"] == "2026-01-01T00:00:00Z", "first_version_timestamp");
    expect(root["versions"][0]["size"] == 10, "first_version_size");
    expect(root["versions"][1]["hash"] == "hash2", "second_version_hash");
}

static void test_action_names() {
    expect(manifest_action_to_string(ManifestActionType::Upload) == "upload", "upload_name");
    expect(manifest_action_to_string(ManifestActionType::Download) == "download", "download_name");
    expect(manifest_action_to_string(ManifestActionType::DeleteLocal) == "delete-local",
           "delete_local_name");
    expect(manifest_action_to_string(ManifestActionType::DeleteRemote) == "delete-remote",
           "delete_remote_name");
    expect(manifest_action_to_string(ManifestActionType::Conflict) == "conflict", "conflict_name");
    expect(manifest_action_to_string(ManifestActionType::Unchanged) == "unchanged",
           "unchanged_name");
}

int main() {
    test_manifest_to_json();
    test_manifest_to_json_keeps_order();
    test_vault_status_to_json();
    test_conflicted_notes_to_json();
    test_file_versions_to_json();
    test_action_names();

    if (failures == 0) {
        std::cout << "All output tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
