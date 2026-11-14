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
#include "vault_status.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

static int exit_code_for(RunStatus status) {
    switch (status) {
    case RunStatus::Success:
        return 0;
    case RunStatus::ActionsFailed:
        return 1;
    case RunStatus::ConflictsPending:
        return 2;
    case RunStatus::DeletesBlocked:
        return 3;
    }

    return 1;
}

static void run_conflict_hook_if_needed(const Config &config, RunStatus status) {
    if (status != RunStatus::ConflictsPending || config.on_conflict_cmd.empty()) {
        return;
    }

    if (std::system(config.on_conflict_cmd.c_str()) != 0) {
        std::cerr << "on-conflict-cmd exited with a non-zero status\n";
    }
}

static void run_watch_loop(const Config &config, SyncService &sync_service) {
    while (true) {
        try {
            run_conflict_hook_if_needed(config, sync_service.run_once());
        } catch (const std::exception &e) {
            std::cerr << "Sync run failed, will retry next interval: " << e.what() << "\n";
        }

        if (!config.quiet) {
            std::cout << "Sleeping for " << config.watch_interval_seconds << " seconds\n";
            std::cout.flush();
        }

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

        if (config.version) {
            std::cout << "obsidian-sync-client " << OBSIDIAN_SYNC_VERSION << "\n";
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

        if (config.status_output) {
            if (!fs::exists(config.vault_path) || !fs::is_directory(config.vault_path)) {
                std::cerr << "Path does not exist or is not a directory\n";
                return 1;
            }

            Database db(config.state_db_path);
            db.initialize();

            VaultStatus status = compute_vault_status(db.load_as_manifest(), config.vault_path);

            if (config.json_output) {
                print_json_vault_status(status);
            } else {
                print_text_vault_status(status);
            }

            return 0;
        }

        if (config.conflicts_output) {
            if (!fs::exists(config.vault_path) || !fs::is_directory(config.vault_path)) {
                std::cerr << "Path does not exist or is not a directory\n";
                return 1;
            }

            auto notes = list_conflicted_notes(config.vault_path);

            Database db(config.state_db_path);
            db.initialize();
            auto records = db.get_unresolved_conflicts();

            std::vector<ConflictDetail> details;

            for (const auto &note : notes) {
                ConflictDetail detail;
                detail.path = note;

                auto record = std::find_if(records.begin(), records.end(),
                                           [&](const ConflictRecord &r) { return r.path == note; });

                if (record != records.end()) {
                    detail.local_hash = record->local_hash;
                    detail.remote_hash = record->remote_hash;
                    detail.detected_time = record->detected_time;
                }

                details.push_back(std::move(detail));
            }

            if (config.json_output) {
                print_json_conflicted_notes(details);
            } else {
                print_text_conflicted_notes(details);
            }

            return 0;
        }

        if (!config.resolve_conflict_path.empty()) {
            if (!fs::exists(config.vault_path) || !fs::is_directory(config.vault_path)) {
                std::cerr << "Path does not exist or is not a directory\n";
                return 1;
            }

            if (!resolve_conflict(config.vault_path, config.resolve_conflict_path,
                                  config.conflict_resolution)) {
                std::cerr << "No unresolved conflict found for " << config.resolve_conflict_path
                          << "\n";
                return 1;
            }

            Database db(config.state_db_path);
            db.initialize();
            db.mark_conflict_resolved(config.resolve_conflict_path,
                                      conflict_resolution_to_string(config.conflict_resolution),
                                      std::chrono::duration_cast<std::chrono::seconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());

            if (!config.quiet) {
                std::cout << "Resolved " << config.resolve_conflict_path << "\n";
            }

            return 0;
        }

        if (!config.history_path.empty() || !config.restore_path.empty() || config.prune_remote) {
            if (!config.insecure_http && !is_secure_remote_url(config.remote_url)) {
                std::cerr << "Refusing plain HTTP to a non-loopback host (would send the bearer "
                             "token in clear text); use https:// or pass --insecure-http\n";
                return 1;
            }

            std::string token = load_required_bearer_token();
            HttpRemoteBackend backend(config.remote_url, token, DEFAULT_MAX_UPLOAD_BYTES,
                                      config.cacert_path);

            if (!config.history_path.empty()) {
                auto versions = backend.history(config.history_path);

                if (config.json_output) {
                    print_json_file_versions(versions);
                } else {
                    print_text_file_versions(versions);
                }

                return 0;
            }

            if (!config.restore_path.empty()) {
                std::string hash = config.restore_hash;

                if (hash.empty()) {
                    auto versions = backend.history(config.restore_path);
                    auto found =
                        std::find_if(versions.begin(), versions.end(), [&](const FileVersion &v) {
                            return v.timestamp <= config.restore_at;
                        });

                    if (found == versions.end()) {
                        std::cerr << "No stored version of " << config.restore_path
                                  << " at or before " << config.restore_at << "\n";
                        return 1;
                    }

                    hash = found->hash;
                }

                backend.restore_version(config.restore_path, hash);

                if (!config.quiet) {
                    std::cout << "Restored " << config.restore_path << " to " << hash << "\n";
                }

                return 0;
            }

            backend.prune();

            if (!config.quiet) {
                std::cout << "Pruned stored file versions\n";
            }

            return 0;
        }

        if (!config.local_root.empty() && !config.remote_url.empty()) {
            if (!fs::exists(config.local_root) || !fs::is_directory(config.local_root)) {
                std::cerr << "Local root does not exist or is not a directory\n";
                return 1;
            }

            if (!config.insecure_http && !is_secure_remote_url(config.remote_url)) {
                std::cerr << "Refusing plain HTTP to a non-loopback host (would send the bearer "
                             "token in clear text); use https:// or pass --insecure-http\n";
                return 1;
            }

            std::string token = load_required_bearer_token();
            HttpRemoteBackend backend(config.remote_url, token, DEFAULT_MAX_UPLOAD_BYTES,
                                      config.cacert_path);
            DeleteSafetyLimits delete_limits{config.force_deletes, config.max_delete_count,
                                             config.max_delete_percent};
            SyncService sync_service(config.local_root, config.state_db_path, backend, config.apply,
                                     delete_limits, config.trash_retention_days, config.sync_config,
                                     config.quiet, config.verbose, config.on_conflict);

            if (config.watch) {
                run_watch_loop(config, sync_service);
                return 0;
            }

            RunStatus status = sync_service.run_once();
            run_conflict_hook_if_needed(config, status);
            return exit_code_for(status);
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
            DeleteSafetyLimits delete_limits{config.force_deletes, config.max_delete_count,
                                             config.max_delete_percent};
            SyncService sync_service(config.local_root, config.state_db_path, backend, config.apply,
                                     delete_limits, config.trash_retention_days, config.sync_config,
                                     config.quiet, config.verbose, config.on_conflict);

            if (config.watch) {
                run_watch_loop(config, sync_service);
                return 0;
            }

            RunStatus status = sync_service.run_once();
            run_conflict_hook_if_needed(config, status);
            return exit_code_for(status);
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

        auto files = scan_vault(root_path, db.load_as_manifest(), config.sync_config);

        if (!config.manifest_file_path.empty()) {
            write_json_manifest_to_file(files, config.manifest_file_path);

            if (!config.quiet) {
                std::cout << "Wrote manifest to " << config.manifest_file_path << "\n";
            }

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
        } else if (!config.quiet) {
            std::cout << "Dry run: database was not updated\n";
        }

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
