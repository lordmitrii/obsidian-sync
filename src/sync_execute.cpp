#include "sync_execute.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static void copy_file_safely(const fs::path &source, const fs::path &destination) {
    fs::create_directories(destination.parent_path());

    fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
}

void execute_manifest_actions(const std::vector<ManifestAction> &actions,
                              const fs::path &local_root, const fs::path &remote_root) {
    for (const auto &action : actions) {
        fs::path local_path = local_root / action.path;
        fs::path remote_path = remote_root / action.path;

        switch (action.type) {
        case ManifestActionType::Upload:
            std::cout << "Uploading " << action.path << "\n";
            copy_file_safely(local_path, remote_path);
            break;

        case ManifestActionType::Download:
            std::cout << "Downloading " << action.path << "\n";
            copy_file_safely(remote_path, local_path);
            break;

        case ManifestActionType::Conflict: {
            std::cout << "Conflict: " << action.path << "\n";

            fs::path local_path = local_root / action.path;
            fs::path remote_path = remote_root / action.path;

            fs::path conflict_path =
                local_path.parent_path() /
                (local_path.stem().string() + ".conflict-remote" + local_path.extension().string());

            copy_file_safely(remote_path, conflict_path);

            std::cout << "Saved remote version as " << conflict_path.string() << "\n";

            break;
        }

        case ManifestActionType::DeleteLocal:
        case ManifestActionType::DeleteRemote:
        case ManifestActionType::Unchanged:
            break;
        }
    }
}
