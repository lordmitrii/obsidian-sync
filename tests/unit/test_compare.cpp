#include "manifest_action.hpp"
#include "three_way_compare.hpp"
#include "two_way_compare.hpp"

#include <iostream>
#include <optional>
#include <string>

static int failures = 0;

static FileMeta make_file(const std::string &path, const std::string &hash) {
    FileMeta meta;
    meta.path = path;
    meta.hash = hash;
    meta.size = hash.size();
    meta.modified_time = 0;
    return meta;
}

static std::optional<ManifestActionType> find_action(const std::vector<ManifestAction> &actions,
                                                      const std::string &path) {
    for (const auto &action : actions) {
        if (action.path == path) {
            return action.type;
        }
    }

    return std::nullopt;
}

static void expect_action(const std::vector<ManifestAction> &actions,
                          const std::string &path,
                          ManifestActionType expected,
                          const std::string &case_name) {
    auto actual = find_action(actions, path);

    if (!actual.has_value()) {
        std::cerr << "FAIL [" << case_name << "]: no action for " << path << "\n";
        ++failures;
        return;
    }

    if (*actual != expected) {
        std::cerr << "FAIL [" << case_name << "]: " << path << " expected "
                  << manifest_action_to_string(expected) << " got "
                  << manifest_action_to_string(*actual) << "\n";
        ++failures;
    }
}

static void expect_absent(const std::vector<ManifestAction> &actions,
                          const std::string &path,
                          const std::string &case_name) {
    if (find_action(actions, path).has_value()) {
        std::cerr << "FAIL [" << case_name << "]: expected no action for " << path << "\n";
        ++failures;
    }
}

static void test_three_way_new_files() {
    Manifest base;
    Manifest local{{"new-local.md", make_file("new-local.md", "h1")}};
    Manifest remote{{"new-remote.md", make_file("new-remote.md", "h2")}};

    auto actions = compare_three_way(base, local, remote);

    expect_action(actions, "new-local.md", ManifestActionType::Upload, "three_way_new_files");
    expect_action(actions, "new-remote.md", ManifestActionType::Download, "three_way_new_files");
}

static void test_three_way_new_on_both_sides() {
    Manifest base;
    Manifest local{{"same.md", make_file("same.md", "h1")}, {"diff.md", make_file("diff.md", "h1")}};
    Manifest remote{{"same.md", make_file("same.md", "h1")}, {"diff.md", make_file("diff.md", "h2")}};

    auto actions = compare_three_way(base, local, remote);

    expect_action(actions, "same.md", ManifestActionType::Unchanged, "three_way_new_on_both_sides");
    expect_action(actions, "diff.md", ManifestActionType::Conflict, "three_way_new_on_both_sides");
}

static void test_three_way_deletions() {
    Manifest base{{"deleted-local.md", make_file("deleted-local.md", "h1")},
                 {"deleted-remote.md", make_file("deleted-remote.md", "h1")},
                 {"deleted-both.md", make_file("deleted-both.md", "h1")},
                 {"delete-vs-edit.md", make_file("delete-vs-edit.md", "h1")}};
    Manifest local{{"deleted-remote.md", make_file("deleted-remote.md", "h1")},
                  {"delete-vs-edit.md", make_file("delete-vs-edit.md", "h2")}};
    Manifest remote{{"deleted-local.md", make_file("deleted-local.md", "h1")}};

    auto actions = compare_three_way(base, local, remote);

    expect_action(actions, "deleted-local.md", ManifestActionType::DeleteRemote, "three_way_deletions");
    expect_action(actions, "deleted-remote.md", ManifestActionType::DeleteLocal, "three_way_deletions");
    expect_action(actions, "deleted-both.md", ManifestActionType::Unchanged, "three_way_deletions");
    expect_action(actions, "delete-vs-edit.md", ManifestActionType::Conflict, "three_way_deletions");
}

static void test_three_way_edits() {
    Manifest base{{"local-edit.md", make_file("local-edit.md", "h1")},
                 {"remote-edit.md", make_file("remote-edit.md", "h1")},
                 {"both-same-edit.md", make_file("both-same-edit.md", "h1")},
                 {"both-conflict-edit.md", make_file("both-conflict-edit.md", "h1")},
                 {"unchanged.md", make_file("unchanged.md", "h1")}};
    Manifest local{{"local-edit.md", make_file("local-edit.md", "h2")},
                  {"remote-edit.md", make_file("remote-edit.md", "h1")},
                  {"both-same-edit.md", make_file("both-same-edit.md", "h2")},
                  {"both-conflict-edit.md", make_file("both-conflict-edit.md", "h2")},
                  {"unchanged.md", make_file("unchanged.md", "h1")}};
    Manifest remote{{"local-edit.md", make_file("local-edit.md", "h1")},
                   {"remote-edit.md", make_file("remote-edit.md", "h2")},
                   {"both-same-edit.md", make_file("both-same-edit.md", "h2")},
                   {"both-conflict-edit.md", make_file("both-conflict-edit.md", "h3")},
                   {"unchanged.md", make_file("unchanged.md", "h1")}};

    auto actions = compare_three_way(base, local, remote);

    expect_action(actions, "local-edit.md", ManifestActionType::Upload, "three_way_edits");
    expect_action(actions, "remote-edit.md", ManifestActionType::Download, "three_way_edits");
    expect_action(actions, "both-same-edit.md", ManifestActionType::Unchanged, "three_way_edits");
    expect_action(actions, "both-conflict-edit.md", ManifestActionType::Conflict, "three_way_edits");
    expect_action(actions, "unchanged.md", ManifestActionType::Unchanged, "three_way_edits");
}

static void test_two_way_compare() {
    Manifest local{{"local-only.md", make_file("local-only.md", "h1")},
                  {"same.md", make_file("same.md", "h1")},
                  {"diff.md", make_file("diff.md", "h1")}};
    Manifest remote{{"remote-only.md", make_file("remote-only.md", "h1")},
                   {"same.md", make_file("same.md", "h1")},
                   {"diff.md", make_file("diff.md", "h2")}};

    auto actions = compare_two_way(local, remote);

    expect_action(actions, "local-only.md", ManifestActionType::Upload, "two_way_compare");
    expect_action(actions, "remote-only.md", ManifestActionType::Download, "two_way_compare");
    expect_action(actions, "diff.md", ManifestActionType::Conflict, "two_way_compare");
    expect_absent(actions, "same.md", "two_way_compare");
}

int main() {
    test_three_way_new_files();
    test_three_way_new_on_both_sides();
    test_three_way_deletions();
    test_three_way_edits();
    test_two_way_compare();

    if (failures == 0) {
        std::cout << "All compare tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
