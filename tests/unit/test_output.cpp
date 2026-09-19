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
    test_action_names();

    if (failures == 0) {
        std::cout << "All output tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
