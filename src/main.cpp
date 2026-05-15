#include "config.hpp"
#include "db.hpp"
#include "manifest.hpp"
#include "manifest_action.hpp"
#include "output.hpp"
#include "scanner.hpp"
#include "sync_execute.hpp"
#include "sync_plan.hpp"
#include "three_way_compare.hpp"
#include "two_way_compare.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static Manifest files_to_manifest(const std::vector<FileMeta> &files) {
    Manifest manifest;

    for (const auto &file : files) {
        manifest[file.path] = file;
    }

    return manifest;
}

int main(int argc, char *argv[]) {
    try {
        Config config = parse_args(argc, argv);

        if (config.compare_manifests) {
            auto local = load_manifest(config.compare_manifest_a);
            auto remote = load_manifest(config.compare_manifest_b);

            auto actions = compare_two_way(local, remote);

            for (const auto &action : actions) {
                std::cout << manifest_action_to_string(action.type) << " " << action.path << "\n";
            }

            return 0;
        }

        if (!config.local_root.empty() && !config.remote_root.empty()) {
            if (!fs::exists(config.local_root) || !fs::is_directory(config.local_root)) {
                std::cerr << "Local root does not exist or is not a directory\n";
                return 1;
            }

            if (!fs::exists(config.remote_root) || !fs::is_directory(config.remote_root)) {
                std::cerr << "Remote root does not exist or is not a directory\n";
                return 1;
            }

            auto local_files = scan_vault(config.local_root);
            auto remote_files = scan_vault(config.remote_root);

            Database db(config.state_db_path);
            db.initialize();

            auto base_manifest = db.load_as_manifest();
            auto local_manifest = files_to_manifest(local_files);
            auto remote_manifest = files_to_manifest(remote_files);

            auto actions = compare_three_way(base_manifest, local_manifest, remote_manifest);

            for (const auto &action : actions) {
                std::cout << manifest_action_to_string(action.type) << " " << action.path << "\n";
            }

            if (config.apply) {
                execute_manifest_actions(actions, config.local_root, config.remote_root);
            }

            return 0;
        }

        fs::path root_path = config.server_mode ? config.server_root : config.vault_path;

        if (!fs::exists(root_path)) {
            std::cerr << "Path does not exist\n";
            return 1;
        }

        if (!fs::is_directory(root_path)) {
            std::cerr << "Path is not a directory\n";
            return 1;
        }

        Database db(config.state_db_path);
        db.initialize();

        auto files = scan_vault(root_path);

        if (!config.manifest_file_path.empty()) {
            write_json_manifest_to_file(files, config.manifest_file_path);
            std::cout << "Wrote manifest to " << config.manifest_file_path << "\n";
            return 0;
        }

        if (config.manifest_output) {
            print_json_manifest(files);
            return 0;
        }

        auto actions = build_sync_plan(db, files);

        if (config.json_output) {
            print_json_actions(actions);
        } else {
            print_text_actions(actions);
        }

        if (!config.dry_run) {
            for (const auto &file : files) {
                db.save_file(file);
            }

            for (const auto &action : actions) {
                if (action.type == SyncActionType::Deleted) {
                    db.delete_file(action.path);
                }
            }
        } else {
            std::cout << "Dry run: database was not updated\n";
        }

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
