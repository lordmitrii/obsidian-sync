#include "config.hpp"
#include "db.hpp"
#include "http_client.hpp"
#include "http_server.hpp"
#include "manifest.hpp"
#include "manifest_action.hpp"
#include "output.hpp"
#include "scanner.hpp"
#include "sync_execute.hpp"
#include "sync_plan.hpp"
#include "three_way_compare.hpp"
#include "two_way_compare.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

static Manifest files_to_manifest(const std::vector<FileMeta> &files) {
    Manifest manifest;

    for (const auto &file : files) {
        manifest[file.path] = file;
    }

    return manifest;
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

static void print_manifest_actions(const std::vector<ManifestAction> &actions) {
    for (const auto &action : actions) {
        std::cout << manifest_action_to_string(action.type) << " " << action.path << "\n";
    }

    std::cout.flush();
}

static void run_http_sync_once(const Config &config) {
    auto local_files = scan_vault(config.local_root);

    Database db(config.state_db_path);
    db.initialize();

    auto base_manifest = db.load_as_manifest();
    auto local_manifest = files_to_manifest(local_files);
    auto remote_manifest = fetch_remote_manifest(config.remote_url);

    auto actions = compare_three_way(base_manifest, local_manifest, remote_manifest);

    print_manifest_actions(actions);

    if (config.apply) {
        execute_http_actions(actions, config.local_root, config.remote_url);
        update_base_state_after_apply(db, actions, config.local_root);
    }
}

static void run_local_sync_once(const Config &config) {
    auto local_files = scan_vault(config.local_root);
    auto remote_files = scan_vault(config.remote_root);

    Database db(config.state_db_path);
    db.initialize();

    auto base_manifest = db.load_as_manifest();
    auto local_manifest = files_to_manifest(local_files);
    auto remote_manifest = files_to_manifest(remote_files);

    auto actions = compare_three_way(base_manifest, local_manifest, remote_manifest);

    print_manifest_actions(actions);

    if (config.apply) {
        execute_manifest_actions(actions, config.local_root, config.remote_root);
        update_base_state_after_apply(db, actions, config.local_root);
    }
}

template <typename SyncFn>
static void run_watch_loop(const Config &config, SyncFn sync_once) {
    while (true) {
        sync_once(config);
        std::cout << "Sleeping for " << config.watch_interval_seconds << " seconds\n";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(config.watch_interval_seconds));
    }
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

        if (!config.local_root.empty() && !config.remote_url.empty()) {
            if (!fs::exists(config.local_root) || !fs::is_directory(config.local_root)) {
                std::cerr << "Local root does not exist or is not a directory\n";
                return 1;
            }

            if (config.watch) {
                run_watch_loop(config, run_http_sync_once);
            } else {
                run_http_sync_once(config);
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

            if (config.watch) {
                run_watch_loop(config, run_local_sync_once);
            } else {
                run_local_sync_once(config);
            }

            return 0;
        }

        if (config.server_mode) {
            if (!fs::exists(config.server_root) || !fs::is_directory(config.server_root)) {
                std::cerr << "Server root does not exist or is not a directory\n";
                return 1;
            }

            run_http_server(config.server_root, config.server_host, config.server_port);
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
