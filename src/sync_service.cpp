#include "sync_service.hpp"

#include "db.hpp"
#include "manifest_action.hpp"
#include "scanner.hpp"
#include "three_way_compare.hpp"

#include <filesystem>
#include <iostream>
#include <utility>

namespace fs = std::filesystem;

static Manifest files_to_manifest(const std::vector<FileMeta> &files) {
    Manifest manifest;

    for (const auto &file : files) {
        manifest[file.path] = file;
    }

    return manifest;
}

static fs::path remote_conflict_path(const fs::path &local_path) {
    return local_path.parent_path() /
           (local_path.stem().string() + ".conflict-remote" + local_path.extension().string());
}

static void print_manifest_actions(const std::vector<ManifestAction> &actions) {
    for (const auto &action : actions) {
        std::cout << manifest_action_to_string(action.type) << " " << action.path << "\n";
    }

    std::cout.flush();
}

static void update_base_state_after_apply(Database &db,
                                          const std::vector<ManifestAction> &actions,
                                          const fs::path &local_root) {
    for (const auto &action : actions) {
        switch (action.type) {
        case ManifestActionType::Upload:
        case ManifestActionType::Download:
        case ManifestActionType::Unchanged: {
            auto file = scan_file(local_root, action.path);

            if (file.has_value()) {
                db.save_file(*file);
            } else {
                db.delete_file(action.path);
            }

            break;
        }

        case ManifestActionType::DeleteLocal:
        case ManifestActionType::DeleteRemote:
            db.delete_file(action.path);
            break;

        case ManifestActionType::Conflict:
            break;
        }
    }
}

static void apply_actions(const std::vector<ManifestAction> &actions,
                          const fs::path &local_root,
                          RemoteBackend &remote_backend) {
    for (const auto &action : actions) {
        fs::path local_path = local_root / action.path;

        switch (action.type) {
        case ManifestActionType::Upload:
            remote_backend.upload(action.path, local_path);
            break;

        case ManifestActionType::Download:
            remote_backend.download(action.path, local_path);
            break;

        case ManifestActionType::DeleteLocal:
            std::cout << "Deleting local " << action.path << "\n";
            fs::remove(local_path);
            break;

        case ManifestActionType::DeleteRemote:
            remote_backend.delete_remote(action.path);
            break;

        case ManifestActionType::Conflict:
            std::cout << "Conflict: " << action.path << "\n";
            remote_backend.save_conflict_copy(action.path, remote_conflict_path(local_path));
            break;

        case ManifestActionType::Unchanged:
            break;
        }
    }
}

SyncService::SyncService(fs::path local_root,
                         std::string state_db_path,
                         RemoteBackend &remote_backend,
                         bool apply)
    : local_root_(std::move(local_root)),
      state_db_path_(std::move(state_db_path)),
      remote_backend_(remote_backend),
      apply_(apply) {}

void SyncService::run_once() {
    Database db(state_db_path_);
    db.initialize();

    auto base_manifest = db.load_as_manifest();
    auto local_manifest = files_to_manifest(scan_vault(local_root_));
    auto remote_manifest = remote_backend_.load_manifest();
    auto actions = compare_three_way(base_manifest, local_manifest, remote_manifest);

    print_manifest_actions(actions);

    if (apply_) {
        apply_actions(actions, local_root_, remote_backend_);
        update_base_state_after_apply(db, actions, local_root_);
    }
}
