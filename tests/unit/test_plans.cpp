#include "db.hpp"
#include "sync_plan.hpp"
#include "two_way_compare.hpp"

#include <algorithm>
#include <iostream>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static FileMeta meta(const std::string &path, const std::string &hash) {
    return {path, hash, 1, 1700000000};
}

static bool has_action(const std::vector<ManifestAction> &actions, ManifestActionType type,
                       const std::string &path) {
    return std::any_of(actions.begin(), actions.end(), [&](const ManifestAction &action) {
        return action.type == type && action.path == path;
    });
}

static bool has_sync_action(const std::vector<SyncAction> &actions, SyncActionType type,
                            const std::string &path) {
    return std::any_of(actions.begin(), actions.end(), [&](const SyncAction &action) {
        return action.type == type && action.path == path;
    });
}

static void test_two_way_compare() {
    Manifest local = {{"only-local.md", meta("only-local.md", "a")},
                      {"same.md", meta("same.md", "b")},
                      {"differs.md", meta("differs.md", "c")}};
    Manifest remote = {{"only-remote.md", meta("only-remote.md", "d")},
                       {"same.md", meta("same.md", "b")},
                       {"differs.md", meta("differs.md", "e")}};

    auto actions = compare_two_way(local, remote);

    expect(actions.size() == 3, "two_way_action_count");
    expect(has_action(actions, ManifestActionType::Upload, "only-local.md"), "two_way_upload");
    expect(has_action(actions, ManifestActionType::Download, "only-remote.md"),
           "two_way_download");
    expect(has_action(actions, ManifestActionType::Conflict, "differs.md"), "two_way_conflict");
    expect(compare_two_way({}, {}).empty(), "two_way_both_empty");
}

static void test_sync_plan_against_database() {
    Database db(":memory:");
    db.initialize();
    db.save_file(meta("unchanged.md", "a"));
    db.save_file(meta("modified.md", "old"));
    db.save_file(meta("removed.md", "gone"));

    std::vector<FileMeta> current = {meta("unchanged.md", "a"), meta("modified.md", "new"),
                                     meta("created.md", "fresh")};

    auto actions = build_sync_plan(db, current);

    expect(actions.size() == 4, "sync_plan_action_count");
    expect(has_sync_action(actions, SyncActionType::Unchanged, "unchanged.md"),
           "sync_plan_unchanged");
    expect(has_sync_action(actions, SyncActionType::Modified, "modified.md"), "sync_plan_modified");
    expect(has_sync_action(actions, SyncActionType::Created, "created.md"), "sync_plan_created");
    expect(has_sync_action(actions, SyncActionType::Deleted, "removed.md"), "sync_plan_deleted");
}

int main() {
    test_two_way_compare();
    test_sync_plan_against_database();

    if (failures == 0) {
        std::cout << "All plan tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
