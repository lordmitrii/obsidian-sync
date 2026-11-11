#include "version_history.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static fs::path fresh_dir(const std::string &name) {
    const auto dir = fs::temp_directory_path() / ("obsidian-sync-version-history-test-" + name);
    fs::remove_all(dir);
    return dir;
}

static void test_record_then_list_round_trips() {
    VersionHistory history(fresh_dir("round-trip"));
    history.record("note.md", "v1");
    history.record("note.md", "v2");

    auto versions = history.list("note.md");
    expect(versions.size() == 2, "record_then_list_round_trips (count)");

    auto newest = history.content(versions[0].hash);
    expect(newest.has_value() && *newest == "v2", "record_then_list_round_trips (newest first)");

    auto oldest = history.content(versions[1].hash);
    expect(oldest.has_value() && *oldest == "v1", "record_then_list_round_trips (content)");
}

static void test_list_on_unknown_path_is_empty() {
    VersionHistory history(fresh_dir("unknown"));
    expect(history.list("missing.md").empty(), "list_on_unknown_path_is_empty");
}

static void test_record_is_a_no_op_for_unchanged_content() {
    VersionHistory history(fresh_dir("no-op"));
    history.record("note.md", "same");
    history.record("note.md", "same");

    expect(history.list("note.md").size() == 1, "record_is_a_no_op_for_unchanged_content");
}

static void test_prune_all_enforces_max_versions_per_file() {
    VersionHistory history(fresh_dir("max-versions"));
    history.record("note.md", "v1");
    history.record("note.md", "v2");
    history.record("note.md", "v3");

    history.prune_all(2, 0);

    auto versions = history.list("note.md");
    expect(versions.size() == 2, "prune_all_enforces_max_versions_per_file (count)");

    auto newest = history.content(versions[0].hash);
    expect(newest.has_value() && *newest == "v3",
          "prune_all_enforces_max_versions_per_file (keeps newest)");
}

static void test_prune_all_drops_orphaned_objects() {
    VersionHistory history(fresh_dir("orphans"));
    history.record("note.md", "v1");
    history.record("note.md", "v2");
    std::string dropped_hash = history.list("note.md")[1].hash;

    history.prune_all(1, 0);

    expect(!history.content(dropped_hash).has_value(), "prune_all_drops_orphaned_objects");
}

static void test_prune_all_on_nonexistent_directory_is_a_no_op() {
    VersionHistory history(fresh_dir("never-written"));
    history.prune_all(5, 30);
}

int main() {
    test_record_then_list_round_trips();
    test_list_on_unknown_path_is_empty();
    test_record_is_a_no_op_for_unchanged_content();
    test_prune_all_enforces_max_versions_per_file();
    test_prune_all_drops_orphaned_objects();
    test_prune_all_on_nonexistent_directory_is_a_no_op();

    if (failures == 0) {
        std::cout << "All version history tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
