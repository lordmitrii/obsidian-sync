#include "manifest_action.hpp"
#include "three_way_compare.hpp"
#include "two_way_compare.hpp"

#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

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

static const std::vector<std::string> kPaths = {"a.md", "b.md", "c.md", "d.md"};
static const std::vector<std::string> kHashPool = {"h1", "h2", "h3"};

static Manifest random_manifest(std::mt19937 &rng) {
    Manifest manifest;
    std::uniform_int_distribution<int> presence(0, 2);
    std::uniform_int_distribution<std::size_t> hash_pick(0, kHashPool.size() - 1);

    for (const auto &path : kPaths) {
        if (presence(rng) == 0) {
            continue;
        }

        manifest[path] = make_file(path, kHashPool[hash_pick(rng)]);
    }

    return manifest;
}

static std::unordered_set<std::string> manifest_paths(const Manifest &manifest) {
    std::unordered_set<std::string> paths;

    for (const auto &[path, file] : manifest) {
        paths.insert(path);
    }

    return paths;
}

static ManifestActionType swap_upload_download_and_deletes(ManifestActionType type) {
    switch (type) {
    case ManifestActionType::Upload:
        return ManifestActionType::Download;
    case ManifestActionType::Download:
        return ManifestActionType::Upload;
    case ManifestActionType::DeleteLocal:
        return ManifestActionType::DeleteRemote;
    case ManifestActionType::DeleteRemote:
        return ManifestActionType::DeleteLocal;
    default:
        return type;
    }
}

static void check_every_union_path_has_exactly_one_action(
    const Manifest &base, const Manifest &local, const Manifest &remote,
    const std::vector<ManifestAction> &actions, const std::string &case_name) {
    std::unordered_set<std::string> expected_paths = manifest_paths(base);
    for (const auto &path : manifest_paths(local))
        expected_paths.insert(path);
    for (const auto &path : manifest_paths(remote))
        expected_paths.insert(path);

    std::unordered_set<std::string> seen;

    for (const auto &action : actions) {
        expect(expected_paths.count(action.path) == 1, case_name + "_action_path_is_in_union");
        expect(seen.insert(action.path).second, case_name + "_no_duplicate_action_per_path");
    }

    expect(seen.size() == expected_paths.size(), case_name + "_every_union_path_got_an_action");
}

static void test_property_every_union_path_covered_once() {
    std::mt19937 rng(12345);

    for (int trial = 0; trial < 300; ++trial) {
        Manifest base = random_manifest(rng);
        Manifest local = random_manifest(rng);
        Manifest remote = random_manifest(rng);

        auto actions = compare_three_way(base, local, remote);
        check_every_union_path_has_exactly_one_action(base, local, remote, actions,
                                                      "coverage_trial_" + std::to_string(trial));
    }
}

static void test_property_swapping_local_and_remote_swaps_the_direction() {
    std::mt19937 rng(67890);

    for (int trial = 0; trial < 300; ++trial) {
        Manifest base = random_manifest(rng);
        Manifest local = random_manifest(rng);
        Manifest remote = random_manifest(rng);

        auto forward = compare_three_way(base, local, remote);
        auto swapped = compare_three_way(base, remote, local);

        for (const auto &action : forward) {
            auto swapped_action = find_action(swapped, action.path);
            std::string case_name = "swap_trial_" + std::to_string(trial) + "_" + action.path;

            expect(swapped_action.has_value(), case_name + "_has_matching_path");

            if (swapped_action.has_value()) {
                expect(*swapped_action == swap_upload_download_and_deletes(action.type), case_name);
            }
        }
    }
}

static void test_property_identical_local_and_remote_never_conflict() {
    std::mt19937 rng(13579);

    for (int trial = 0; trial < 300; ++trial) {
        Manifest base = random_manifest(rng);
        Manifest local = random_manifest(rng);

        auto actions = compare_three_way(base, local, local);

        for (const auto &action : actions) {
            expect(action.type == ManifestActionType::Unchanged, "identical_sides_trial_" +
                                                                     std::to_string(trial) + "_" +
                                                                     action.path + "_is_unchanged");
        }
    }
}

static void test_property_first_sync_from_empty_base() {
    std::mt19937 rng(24680);
    Manifest base;

    for (int trial = 0; trial < 300; ++trial) {
        Manifest local = random_manifest(rng);
        Manifest remote = random_manifest(rng);

        auto actions = compare_three_way(base, local, remote);
        std::string case_name = "first_sync_trial_" + std::to_string(trial);

        for (const auto &path : kPaths) {
            auto local_it = local.find(path);
            auto remote_it = remote.find(path);
            bool in_local = local_it != local.end();
            bool in_remote = remote_it != remote.end();
            auto action = find_action(actions, path);

            if (in_local && !in_remote) {
                expect(action == ManifestActionType::Upload, case_name + "_" + path + "_upload");
            } else if (!in_local && in_remote) {
                expect(action == ManifestActionType::Download,
                       case_name + "_" + path + "_download");
            } else if (in_local && in_remote) {
                bool same_hash = local_it->second.hash == remote_it->second.hash;
                expect(action == (same_hash ? ManifestActionType::Unchanged
                                            : ManifestActionType::Conflict),
                       case_name + "_" + path + "_matches_hash_comparison");
            } else {
                expect(!action.has_value(), case_name + "_" + path + "_absent_everywhere");
            }
        }
    }
}

static void test_property_delete_never_drops_a_changed_side() {
    std::mt19937 rng(97531);

    for (int trial = 0; trial < 300; ++trial) {
        Manifest base = random_manifest(rng);
        Manifest local = random_manifest(rng);
        Manifest remote = random_manifest(rng);

        auto actions = compare_three_way(base, local, remote);
        std::string case_name = "no_delete_of_changed_side_trial_" + std::to_string(trial);

        for (const auto &action : actions) {
            auto base_it = base.find(action.path);

            if (action.type == ManifestActionType::DeleteLocal) {
                auto local_it = local.find(action.path);
                expect(base_it != base.end() && local_it != local.end() &&
                           base_it->second.hash == local_it->second.hash,
                       case_name + "_" + action.path + "_delete_local_keeps_unchanged_side");
            } else if (action.type == ManifestActionType::DeleteRemote) {
                auto remote_it = remote.find(action.path);
                expect(base_it != base.end() && remote_it != remote.end() &&
                           base_it->second.hash == remote_it->second.hash,
                       case_name + "_" + action.path + "_delete_remote_keeps_unchanged_side");
            }
        }
    }
}

static void apply_action_to_manifests(const ManifestAction &action, const Manifest &local,
                                      const Manifest &remote, Manifest &new_base,
                                      Manifest &new_local, Manifest &new_remote) {
    switch (action.type) {
    case ManifestActionType::Upload:
        new_remote[action.path] = local.at(action.path);
        new_base[action.path] = local.at(action.path);
        break;
    case ManifestActionType::Download:
        new_local[action.path] = remote.at(action.path);
        new_base[action.path] = remote.at(action.path);
        break;
    case ManifestActionType::DeleteLocal:
        new_local.erase(action.path);
        new_base.erase(action.path);
        break;
    case ManifestActionType::DeleteRemote:
        new_remote.erase(action.path);
        new_base.erase(action.path);
        break;
    case ManifestActionType::Unchanged: {
        auto local_it = local.find(action.path);

        if (local_it != local.end()) {
            new_base[action.path] = local_it->second;
        } else {
            new_base.erase(action.path);
        }

        break;
    }
    case ManifestActionType::Conflict:
        break;
    }
}

static void test_property_plan_is_idempotent_once_applied() {
    std::mt19937 rng(864209);

    for (int trial = 0; trial < 300; ++trial) {
        Manifest base = random_manifest(rng);
        Manifest local = random_manifest(rng);
        Manifest remote = random_manifest(rng);

        auto first_run = compare_three_way(base, local, remote);

        Manifest new_base = base;
        Manifest new_local = local;
        Manifest new_remote = remote;

        for (const auto &action : first_run) {
            apply_action_to_manifests(action, local, remote, new_base, new_local, new_remote);
        }

        auto second_run = compare_three_way(new_base, new_local, new_remote);
        std::string case_name = "idempotent_trial_" + std::to_string(trial);

        for (const auto &action : first_run) {
            auto second_action = find_action(second_run, action.path);

            if (action.type == ManifestActionType::Conflict) {
                expect(second_action == ManifestActionType::Conflict,
                       case_name + "_" + action.path + "_conflict_stays_a_conflict");
            } else {
                // A resolved path either converges to Unchanged (both sides still hold it) or
                // drops out of the union entirely (a delete that left nothing anywhere) - either
                // way it must never come back as Upload/Download/Delete/Conflict.
                expect(!second_action.has_value() ||
                           *second_action == ManifestActionType::Unchanged,
                       case_name + "_" + action.path + "_never_resurfaces_as_a_pending_change");
            }
        }
    }
}

static ManifestActionType swap_upload_download(ManifestActionType type) {
    switch (type) {
    case ManifestActionType::Upload:
        return ManifestActionType::Download;
    case ManifestActionType::Download:
        return ManifestActionType::Upload;
    default:
        return type;
    }
}

static void test_property_two_way_swap_swaps_the_direction() {
    std::mt19937 rng(112233);

    for (int trial = 0; trial < 300; ++trial) {
        Manifest local = random_manifest(rng);
        Manifest remote = random_manifest(rng);

        auto forward = compare_two_way(local, remote);
        auto swapped = compare_two_way(remote, local);
        std::string case_name = "two_way_swap_trial_" + std::to_string(trial);

        expect(forward.size() == swapped.size(), case_name + "_same_action_count");

        for (const auto &action : forward) {
            auto swapped_action = find_action(swapped, action.path);
            expect(swapped_action.has_value(), case_name + "_" + action.path + "_has_match");

            if (swapped_action.has_value()) {
                expect(*swapped_action == swap_upload_download(action.type),
                       case_name + "_" + action.path);
            }
        }
    }
}

static void test_property_two_way_identical_manifests_produce_no_actions() {
    std::mt19937 rng(445566);

    for (int trial = 0; trial < 300; ++trial) {
        Manifest manifest = random_manifest(rng);

        auto actions = compare_two_way(manifest, manifest);
        expect(actions.empty(), "two_way_identical_trial_" + std::to_string(trial) + "_no_actions");
    }
}

static void test_property_two_way_classifies_every_path_correctly() {
    std::mt19937 rng(778899);

    for (int trial = 0; trial < 300; ++trial) {
        Manifest local = random_manifest(rng);
        Manifest remote = random_manifest(rng);

        auto actions = compare_two_way(local, remote);
        std::string case_name = "two_way_classify_trial_" + std::to_string(trial);

        for (const auto &path : kPaths) {
            auto local_it = local.find(path);
            auto remote_it = remote.find(path);
            bool in_local = local_it != local.end();
            bool in_remote = remote_it != remote.end();
            auto action = find_action(actions, path);

            if (in_local && !in_remote) {
                expect(action == ManifestActionType::Upload, case_name + "_" + path + "_upload");
            } else if (!in_local && in_remote) {
                expect(action == ManifestActionType::Download,
                       case_name + "_" + path + "_download");
            } else if (in_local && in_remote) {
                bool same_hash = local_it->second.hash == remote_it->second.hash;
                expect(same_hash ? !action.has_value() : action == ManifestActionType::Conflict,
                       case_name + "_" + path + "_matches_hash_comparison");
            } else {
                expect(!action.has_value(), case_name + "_" + path + "_absent_everywhere");
            }
        }
    }
}

int main() {
    test_property_every_union_path_covered_once();
    test_property_swapping_local_and_remote_swaps_the_direction();
    test_property_identical_local_and_remote_never_conflict();
    test_property_first_sync_from_empty_base();
    test_property_delete_never_drops_a_changed_side();
    test_property_plan_is_idempotent_once_applied();
    test_property_two_way_swap_swaps_the_direction();
    test_property_two_way_identical_manifests_produce_no_actions();
    test_property_two_way_classifies_every_path_correctly();

    if (failures == 0) {
        std::cout << "All compare property tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
