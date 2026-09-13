#include "config.hpp"
#include "db.hpp"
#include "http_remote_backend.hpp"
#include "local_remote_backend.hpp"
#include "manifest.hpp"
#include "manifest_action.hpp"
#include "output.hpp"
#include "scanner.hpp"
#include "security.hpp"
#include "sync_plan.hpp"
#include "sync_service.hpp"
#include "two_way_compare.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

static void run_watch_loop(const Config &config, SyncService &sync_service) {
    while (true) {
        sync_service.run_once();
        std::cout << "Sleeping for " << config.watch_interval_seconds << " seconds\n";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(config.watch_interval_seconds));
    }
}

int main(int argc, char *argv[]) {
    try {
        Config config = parse_args(argc, argv);

        if (config.help) {
            print_usage();
            return 0;
        }

        if (config.server_mode) {
            std::cerr << "Use obsidian-sync-server for --server-root\n";
            return 1;
        }

        if (config.compare_manifests) {
            auto local = load_manifest(config.compare_manifest_a);
            auto remote = load_manifest(config.compare_manifest_b);

            auto actions = compare_two_way(local, remote);

            for (const auto &action : actions) {
                std::cout << manifest_action_to_string(action.type) << " " << action.path << "\n";
            }

            return 0;
        }

        if (!config.local_root.empty() && !config.remote_url.empty()) {
            if (!fs::exists(config.local_root) || !fs::is_directory(config.local_root)) {
                std::cerr << "Local root does not exist or is not a directory\n";
                return 1;
            }

            std::string token = load_required_bearer_token();
            HttpRemoteBackend backend(config.remote_url, token, DEFAULT_MAX_UPLOAD_BYTES);
            SyncService sync_service(config.local_root, config.state_db_path, backend, config.apply);

            if (config.watch) {
                run_watch_loop(config, sync_service);
            } else {
                sync_service.run_once();
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

            LocalRemoteBackend backend(config.remote_root);
            SyncService sync_service(config.local_root, config.state_db_path, backend, config.apply);

            if (config.watch) {
                run_watch_loop(config, sync_service);
            } else {
                sync_service.run_once();
            }

            return 0;
        }

        fs::path root_path = config.vault_path;

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
