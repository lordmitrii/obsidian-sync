#include "sync_service.hpp"

#include "atomic_file.hpp"
#include "db.hpp"
#include "hasher.hpp"
#include "manifest_action.hpp"
#include "scanner.hpp"
#include "three_way_compare.hpp"

#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
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

// Numbers past the first conflict copy: name.conflict-remote.ext,
// name.conflict-remote.2.ext, name.conflict-remote.3.ext, ...
static fs::path numbered_conflict_path(const fs::path &first_choice, int n) {
    return first_choice.parent_path() /
           (first_choice.stem().string() + "." + std::to_string(n) +
            first_choice.extension().string());
}

static void print_manifest_actions(const std::vector<ManifestAction> &actions) {
    for (const auto &action : actions) {
        std::cout << manifest_action_to_string(action.type) << " " << action.path << "\n";
    }

    std::cout.flush();
}

static size_t count_deletes(const std::vector<ManifestAction> &actions) {
    size_t count = 0;

    for (const auto &action : actions) {
        if (action.type == ManifestActionType::DeleteLocal ||
            action.type == ManifestActionType::DeleteRemote) {
            ++count;
        }
    }

    return count;
}

static constexpr size_t kMinBaseSizeForPercentLimit = 10;

static bool exceeds_delete_limits(size_t delete_count,
                                  size_t base_size,
                                  const DeleteSafetyLimits &limits) {
    if (limits.force || delete_count == 0) {
        return false;
    }

    if (delete_count > limits.max_count) {
        return true;
    }

    if (base_size < kMinBaseSizeForPercentLimit) {
        return false;
    }

    double percent = 100.0 * delete_count / base_size;
    return percent > limits.max_percent;
}

// Below kMinBaseSizeForPercentLimit, a fully emptied remote can be a
// legitimate delete of the whole (tiny) vault, same reasoning as the percent
// check's exemption below. At or above it, every previously known file
// vanishing at once looks like a misconfigured --server-root/--remote-url or
// an empty backup, not something a user does on purpose in one run.
static bool remote_manifest_suspiciously_empty(const Manifest &remote_manifest,
                                               const Manifest &base_manifest) {
    return remote_manifest.empty() && base_manifest.size() >= kMinBaseSizeForPercentLimit;
}

static size_t report_unresolved_conflicts(const std::vector<ManifestAction> &actions) {
    std::vector<std::string> paths;

    for (const auto &action : actions) {
        if (action.type == ManifestActionType::Conflict) {
            paths.push_back(action.path);
        }
    }

    if (!paths.empty()) {
        std::cerr << paths.size() << " unresolved conflict(s):\n";

        for (const auto &path : paths) {
            std::cerr << "  " << path << "\n";
        }
    }

    return paths.size();
}

// Base state is recorded from the plan-time manifests (what was actually
// compared and transferred), never by re-reading the file from disk — a
// re-read could see an edit the user made after the scan, which would wrongly
// promote that edit to "base" without it ever having reached the remote.
static void update_base_state_after_apply(Database &db,
                                          const std::vector<ManifestAction> &actions,
                                          const Manifest &local_manifest,
                                          const Manifest &remote_manifest,
                                          const std::set<std::string> &failed_paths) {
    for (const auto &action : actions) {
        if (failed_paths.count(action.path) > 0) {
            continue;
        }

        switch (action.type) {
        case ManifestActionType::Upload:
        case ManifestActionType::Unchanged: {
            auto it = local_manifest.find(action.path);

            if (it != local_manifest.end()) {
                db.save_file(it->second);
            } else {
                db.delete_file(action.path);
            }

            break;
        }

        case ManifestActionType::Download: {
            auto it = remote_manifest.find(action.path);

            if (it != remote_manifest.end()) {
                db.save_file(it->second);
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

static void apply_action(const ManifestAction &action,
                         const fs::path &local_root,
                         const Manifest &local_manifest,
                         const Manifest &remote_manifest,
                         RemoteBackend &remote_backend) {
    fs::path local_path = local_root / action.path;

    switch (action.type) {
    case ManifestActionType::Upload: {
        // Re-hash right before sending: if the file changed since it was
        // planned, skip it so it's retried (and re-planned) next run instead
        // of uploading stale bytes and recording them as the new base.
        auto planned = local_manifest.find(action.path);

        if (planned != local_manifest.end()) {
            auto current = scan_file(local_root, action.path);

            if (!current.has_value() || current->hash != planned->second.hash) {
                throw std::runtime_error("file changed since it was planned, will retry");
            }
        }

        remote_backend.upload(action.path, local_path);
        break;
    }

    case ManifestActionType::Download:
        remote_backend.download(action.path, local_path);
        break;

    case ManifestActionType::DeleteLocal:
        std::cout << "Moving local " << action.path << " to .trash\n";
        move_to_trash(local_path, local_root / ".trash", action.path);
        break;

    case ManifestActionType::DeleteRemote:
        remote_backend.delete_remote(action.path);
        break;

    case ManifestActionType::Conflict: {
        std::cout << "Conflict: " << action.path << "\n";

        fs::path conflict_path = remote_conflict_path(local_path);
        auto remote_it = remote_manifest.find(action.path);

        // A conflict copy from an earlier run already sitting at
        // conflict_path is expected in --watch mode: base state doesn't
        // move forward on conflict, so the same conflict is replanned every
        // interval. Skip re-downloading it when it still matches the
        // current remote hash, and give it a fresh numbered name instead of
        // clobbering it when the remote has since changed again.
        if (fs::exists(conflict_path)) {
            if (remote_it != remote_manifest.end() &&
                sha256_file(conflict_path) == remote_it->second.hash) {
                std::cout << "Conflict copy already up to date, skipping\n";
                break;
            }

            for (int n = 2; fs::exists(conflict_path); ++n) {
                conflict_path = numbered_conflict_path(remote_conflict_path(local_path), n);
            }
        }

        remote_backend.save_conflict_copy(action.path, conflict_path);
        break;
    }

    case ManifestActionType::Unchanged:
        break;
    }
}

// Applies every action independently: a failing action is logged and
// skipped rather than aborting the whole run, so one bad file doesn't block
// every action sorted after it. Returns the paths that failed, so the
// caller can leave their base state alone and retry them next run.
static std::set<std::string> apply_actions(const std::vector<ManifestAction> &actions,
                                           const fs::path &local_root,
                                           const Manifest &local_manifest,
                                           const Manifest &remote_manifest,
                                           RemoteBackend &remote_backend) {
    std::set<std::string> failed_paths;

    for (const auto &action : actions) {
        try {
            apply_action(action, local_root, local_manifest, remote_manifest, remote_backend);
        } catch (const std::exception &e) {
            std::cerr << "Failed to apply " << manifest_action_to_string(action.type) << " "
                      << action.path << ": " << e.what() << "\n";
            failed_paths.insert(action.path);
        }
    }

    return failed_paths;
}

SyncService::SyncService(fs::path local_root,
                         std::string state_db_path,
                         RemoteBackend &remote_backend,
                         bool apply,
                         DeleteSafetyLimits delete_limits,
                         int trash_retention_days)
    : local_root_(std::move(local_root)),
      state_db_path_(std::move(state_db_path)),
      remote_backend_(remote_backend),
      apply_(apply),
      delete_limits_(delete_limits),
      trash_retention_days_(trash_retention_days) {}

RunStatus SyncService::run_once() {
    Database db(state_db_path_);
    db.initialize();

    auto base_manifest = db.load_as_manifest();
    auto local_manifest = files_to_manifest(scan_vault(local_root_, base_manifest));
    auto remote_manifest = remote_backend_.load_manifest();
    auto actions = compare_three_way(base_manifest, local_manifest, remote_manifest);

    print_manifest_actions(actions);

    size_t conflict_count = report_unresolved_conflicts(actions);

    if (!apply_) {
        return conflict_count > 0 ? RunStatus::ConflictsPending : RunStatus::Success;
    }

    if (!delete_limits_.force &&
        remote_manifest_suspiciously_empty(remote_manifest, base_manifest)) {
        std::cerr << "Refusing to apply: remote manifest is empty but " << base_manifest.size()
                  << " file(s) are known from a previous sync (a misconfigured --server-root/"
                     "--remote-url or an empty backup can look identical). Re-run with "
                     "--force-deletes if this is expected.\n";
        return RunStatus::DeletesBlocked;
    }

    size_t delete_count = count_deletes(actions);

    if (exceeds_delete_limits(delete_count, base_manifest.size(), delete_limits_)) {
        std::cerr << "Refusing to apply: plan deletes " << delete_count << " of "
                  << base_manifest.size()
                  << " known file(s), above the safety limit (--max-deletes "
                  << delete_limits_.max_count << ", --max-delete-percent "
                  << delete_limits_.max_percent
                  << "). Re-run with --force-deletes if this is expected.\n";
        return RunStatus::DeletesBlocked;
    }

    auto failed_paths =
        apply_actions(actions, local_root_, local_manifest, remote_manifest, remote_backend_);
    update_base_state_after_apply(db, actions, local_manifest, remote_manifest, failed_paths);
    cleanup_trash(local_root_ / ".trash", trash_retention_days_);

    if (!failed_paths.empty()) {
        std::cerr << failed_paths.size() << " action(s) failed and will be retried next run\n";
        return RunStatus::ActionsFailed;
    }

    return conflict_count > 0 ? RunStatus::ConflictsPending : RunStatus::Success;
}
