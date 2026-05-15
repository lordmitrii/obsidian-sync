#include "sync_plan.hpp"

#include <unordered_set>

std::string action_type_to_string(SyncActionType type) {
    switch (type) {
        case SyncActionType::Created:
            return "created";
        case SyncActionType::Modified:
            return "modified";
        case SyncActionType::Unchanged:
            return "same";
        case SyncActionType::Deleted:
            return "deleted";
    }

    return "unknown";
}

std::vector<SyncAction> build_sync_plan(
    Database& db,
    const std::vector<FileMeta>& current_files
) {
    std::vector<SyncAction> actions;
    std::unordered_set<std::string> current_paths;

    for (const auto& file : current_files) {
        current_paths.insert(file.path);

        auto old_file = db.get_file(file.path);

        if (!old_file.has_value()) {
            actions.push_back({SyncActionType::Created, file.path});
        } else if (old_file->hash != file.hash) {
            actions.push_back({SyncActionType::Modified, file.path});
        } else {
            actions.push_back({SyncActionType::Unchanged, file.path});
        }
    }

    auto old_paths = db.get_all_paths();

    for (const auto& old_path : old_paths) {
        if (current_paths.find(old_path) == current_paths.end()) {
            actions.push_back({SyncActionType::Deleted, old_path});
        }
    }

    return actions;
}