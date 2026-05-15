#include "three_way_compare.hpp"

#include <algorithm>
#include <unordered_set>

static bool same_hash(const FileMeta& left, const FileMeta& right) {
    return left.hash == right.hash;
}

std::vector<ManifestAction> compare_three_way(
    const Manifest& base,
    const Manifest& local,
    const Manifest& remote
) {
    std::unordered_set<std::string> path_set;

    for (const auto& [path, file] : base) {
        path_set.insert(path);
    }

    for (const auto& [path, file] : local) {
        path_set.insert(path);
    }

    for (const auto& [path, file] : remote) {
        path_set.insert(path);
    }

    std::vector<std::string> paths(path_set.begin(), path_set.end());
    std::sort(paths.begin(), paths.end());

    std::vector<ManifestAction> actions;

    for (const auto& path : paths) {
        auto base_it = base.find(path);
        auto local_it = local.find(path);
        auto remote_it = remote.find(path);

        const bool in_base = base_it != base.end();
        const bool in_local = local_it != local.end();
        const bool in_remote = remote_it != remote.end();

        if (!in_base) {
            if (in_local && !in_remote) {
                actions.push_back({ManifestActionType::Upload, path});
            } else if (!in_local && in_remote) {
                actions.push_back({ManifestActionType::Download, path});
            } else if (in_local && in_remote) {
                if (same_hash(local_it->second, remote_it->second)) {
                    actions.push_back({ManifestActionType::Unchanged, path});
                } else {
                    actions.push_back({ManifestActionType::Conflict, path});
                }
            }

            continue;
        }

        if (!in_local && !in_remote) {
            actions.push_back({ManifestActionType::Unchanged, path});
            continue;
        }

        if (!in_local) {
            if (same_hash(base_it->second, remote_it->second)) {
                actions.push_back({ManifestActionType::DeleteRemote, path});
            } else {
                actions.push_back({ManifestActionType::Conflict, path});
            }

            continue;
        }

        if (!in_remote) {
            if (same_hash(base_it->second, local_it->second)) {
                actions.push_back({ManifestActionType::DeleteLocal, path});
            } else {
                actions.push_back({ManifestActionType::Conflict, path});
            }

            continue;
        }

        const bool local_changed = !same_hash(base_it->second, local_it->second);
        const bool remote_changed = !same_hash(base_it->second, remote_it->second);

        if (!local_changed && !remote_changed) {
            actions.push_back({ManifestActionType::Unchanged, path});
        } else if (local_changed && !remote_changed) {
            actions.push_back({ManifestActionType::Upload, path});
        } else if (!local_changed && remote_changed) {
            actions.push_back({ManifestActionType::Download, path});
        } else if (same_hash(local_it->second, remote_it->second)) {
            actions.push_back({ManifestActionType::Unchanged, path});
        } else {
            actions.push_back({ManifestActionType::Conflict, path});
        }
    }

    return actions;
}
