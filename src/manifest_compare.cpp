#include "manifest_compare.hpp"

std::string manifest_action_to_string(ManifestActionType type) {
    switch (type) {
        case ManifestActionType::Upload:
            return "upload";
        case ManifestActionType::Download:
            return "download";
        case ManifestActionType::Conflict:
            return "conflict";
    }

    return "unknown";
}

std::vector<ManifestAction> compare_manifests(
    const Manifest& local,
    const Manifest& remote
) {
    std::vector<ManifestAction> actions;

    for (const auto& [path, local_file] : local) {
        auto it = remote.find(path);

        if (it == remote.end()) {
            actions.push_back({ManifestActionType::Upload, path});
            continue;
        }

        const auto& remote_file = it->second;

        if (local_file.hash != remote_file.hash) {
            if (local_file.modified_time > remote_file.modified_time) {
                actions.push_back({ManifestActionType::Upload, path});
            } else if (local_file.modified_time < remote_file.modified_time) {
                actions.push_back({ManifestActionType::Download, path});
            } else {
                actions.push_back({ManifestActionType::Conflict, path});
            }
        }
    }

    for (const auto& [path, remote_file] : remote) {
        if (local.find(path) == local.end()) {
            actions.push_back({ManifestActionType::Download, path});
        }
    }

    return actions;
}