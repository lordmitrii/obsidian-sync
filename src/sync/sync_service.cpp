#include "sync_service.hpp"

#include "atomic_file.hpp"
#include "db.hpp"
#include "hasher.hpp"
#include "manifest_action.hpp"
#include "object_store.hpp"
#include "scanner.hpp"
#include "text_merge.hpp"
#include "three_way_compare.hpp"
#include "vault_status.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

static bool is_markdown_path(const std::string &path) {
    return fs::path(path).extension() == ".md";
}

static std::string read_text_file(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static Manifest files_to_manifest(const std::vector<FileMeta> &files) {
    Manifest manifest;

    for (const auto &file : files) {
        manifest[file.path] = file;
    }

    return manifest;
}

struct ConflictCopySlot {
    fs::path path;
    bool up_to_date;
};

static ConflictCopySlot pick_conflict_copy_slot(const fs::path &local_path,
                                                const std::string *remote_hash) {
    for (int n = 1;; ++n) {
        fs::path candidate = conflict_copy_path(local_path, n);

        if (!fs::exists(candidate)) {
            return {candidate, false};
        }

        if (remote_hash != nullptr && sha256_file(candidate) == *remote_hash) {
            return {candidate, true};
        }
    }
}

static void print_manifest_actions(const std::vector<ManifestAction> &actions,
                                   bool quiet,
                                   bool verbose) {
    if (quiet) {
        return;
    }

    for (const auto &action : actions) {
        if (action.type == ManifestActionType::Unchanged && !verbose) {
            continue;
        }

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

static size_t report_unresolved_conflicts(Database &db,
                                          const std::vector<ManifestAction> &actions,
                                          const Manifest &local_manifest,
                                          const Manifest &remote_manifest,
                                          const std::set<std::string> &merged_paths = {}) {
    std::vector<std::string> paths;
    auto detected_time = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();

    for (const auto &action : actions) {
        if (action.type == ManifestActionType::Conflict) {
            if (merged_paths.count(action.path) > 0) {
                continue;
            }

            paths.push_back(action.path);

            auto local_it = local_manifest.find(action.path);
            auto remote_it = remote_manifest.find(action.path);
            db.record_conflict(action.path,
                               local_it != local_manifest.end() ? local_it->second.hash : "",
                               remote_it != remote_manifest.end() ? remote_it->second.hash : "",
                               detected_time);
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
static void remember_merge_base_if_markdown(const ObjectStore &object_store,
                                            const fs::path &local_root,
                                            const FileMeta &meta) {
    if (is_markdown_path(meta.path)) {
        object_store.put(read_text_file(local_root / meta.path));
    }
}

static void update_base_state_after_apply(Database &db,
                                          const std::vector<ManifestAction> &actions,
                                          const Manifest &local_manifest,
                                          const Manifest &remote_manifest,
                                          const std::set<std::string> &failed_paths,
                                          const std::set<std::string> &merged_paths,
                                          const fs::path &local_root,
                                          const ObjectStore &object_store) {
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
                remember_merge_base_if_markdown(object_store, local_root, it->second);
            } else {
                db.delete_file(action.path);
            }

            break;
        }

        case ManifestActionType::Download: {
            auto it = remote_manifest.find(action.path);

            if (it != remote_manifest.end()) {
                db.save_file(it->second);
                remember_merge_base_if_markdown(object_store, local_root, it->second);
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
            if (merged_paths.count(action.path) > 0) {
                auto merged_meta = scan_file(local_root, action.path);

                if (merged_meta.has_value()) {
                    db.save_file(*merged_meta);
                    remember_merge_base_if_markdown(object_store, local_root, *merged_meta);
                }
            }

            break;
        }
    }
}

static bool try_merge_conflict(const std::string &path,
                               const fs::path &local_root,
                               const Manifest &base_manifest,
                               RemoteBackend &remote_backend,
                               const ObjectStore &object_store) {
    if (!is_markdown_path(path)) {
        return false;
    }

    auto base_it = base_manifest.find(path);

    if (base_it == base_manifest.end()) {
        return false;
    }

    auto base_content = object_store.get(base_it->second.hash);

    if (!base_content.has_value()) {
        return false;
    }

    fs::path local_path = local_root / path;
    std::string ours_content = read_text_file(local_path);

    fs::path remote_temp = local_path;
    remote_temp += ".osync-merge.tmp";

    try {
        remote_backend.download(path, remote_temp);
    } catch (const std::exception &) {
        return false;
    }

    std::string theirs_content = read_text_file(remote_temp);
    fs::remove(remote_temp);

    MergeResult merged = merge_three_way_text(*base_content, ours_content, theirs_content);

    if (merged.status == MergeStatus::Conflicted) {
        return false;
    }

    atomic_write_file(local_path, merged.content);
    remote_backend.upload(path, local_path);
    std::cout << "Merged " << path << " cleanly\n";
    return true;
}

static bool apply_action(const ManifestAction &action,
                         const fs::path &local_root,
                         const Manifest &base_manifest,
                         const Manifest &local_manifest,
                         const Manifest &remote_manifest,
                         RemoteBackend &remote_backend,
                         const ObjectStore &object_store,
                         OnConflictPolicy on_conflict) {
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
        return false;
    }

    case ManifestActionType::Download:
        remote_backend.download(action.path, local_path);
        return false;

    case ManifestActionType::DeleteLocal:
        std::cout << "Moving local " << action.path << " to .trash\n";
        move_to_trash(local_path, local_root / ".trash", action.path);
        return false;

    case ManifestActionType::DeleteRemote:
        remote_backend.delete_remote(action.path);
        return false;

    case ManifestActionType::Conflict: {
        std::cout << "Conflict: " << action.path << "\n";

        if (on_conflict == OnConflictPolicy::Merge &&
            try_merge_conflict(action.path, local_root, base_manifest, remote_backend, object_store)) {
            return true;
        }

        // Conflict copies from earlier runs are expected in --watch mode: base
        // state doesn't move forward on conflict, so the same conflict is
        // replanned every interval. Reuse any copy that already matches the
        // current remote hash, and otherwise take the first free name so an
        // older copy is never clobbered.
        auto remote_it = remote_manifest.find(action.path);
        const std::string *remote_hash =
            remote_it != remote_manifest.end() ? &remote_it->second.hash : nullptr;
        ConflictCopySlot slot = pick_conflict_copy_slot(local_path, remote_hash);

        if (slot.up_to_date) {
            std::cout << "Conflict copy already up to date, skipping\n";
            return false;
        }

        remote_backend.save_conflict_copy(action.path, slot.path);
        return false;
    }

    case ManifestActionType::Unchanged:
        return false;
    }

    return false;
}

struct ApplyOutcome {
    std::set<std::string> failed_paths;
    std::set<std::string> merged_paths;
};

// Applies every action independently: a failing action is logged and
// skipped rather than aborting the whole run, so one bad file doesn't block
// every action sorted after it. Returns the paths that failed, so the
// caller can leave their base state alone and retry them next run.
static ApplyOutcome apply_actions(const std::vector<ManifestAction> &actions,
                                  const fs::path &local_root,
                                  const Manifest &base_manifest,
                                  const Manifest &local_manifest,
                                  const Manifest &remote_manifest,
                                  RemoteBackend &remote_backend,
                                  const ObjectStore &object_store,
                                  OnConflictPolicy on_conflict) {
    ApplyOutcome outcome;

    for (const auto &action : actions) {
        try {
            bool merged = apply_action(action, local_root, base_manifest, local_manifest,
                                       remote_manifest, remote_backend, object_store, on_conflict);

            if (merged) {
                outcome.merged_paths.insert(action.path);
            }
        } catch (const std::exception &e) {
            std::cerr << "Failed to apply " << manifest_action_to_string(action.type) << " "
                      << action.path << ": " << e.what() << "\n";
            outcome.failed_paths.insert(action.path);
        }
    }

    return outcome;
}

SyncService::SyncService(fs::path local_root,
                         std::string state_db_path,
                         RemoteBackend &remote_backend,
                         bool apply,
                         DeleteSafetyLimits delete_limits,
                         int trash_retention_days,
                         SyncConfigPreset sync_config,
                         bool quiet,
                         bool verbose,
                         OnConflictPolicy on_conflict)
    : local_root_(std::move(local_root)),
      state_db_path_(std::move(state_db_path)),
      remote_backend_(remote_backend),
      apply_(apply),
      delete_limits_(delete_limits),
      trash_retention_days_(trash_retention_days),
      sync_config_(sync_config),
      quiet_(quiet),
      verbose_(verbose),
      on_conflict_(on_conflict),
      object_store_(fs::path(state_db_path_).parent_path() / "objects") {}

RunStatus SyncService::run_once() {
    Database db(state_db_path_);
    db.initialize();

    auto base_manifest = db.load_as_manifest();
    auto local_manifest = files_to_manifest(scan_vault(local_root_, base_manifest, sync_config_));
    auto remote_manifest = remote_backend_.load_manifest();
    auto actions = compare_three_way(base_manifest, local_manifest, remote_manifest);

    print_manifest_actions(actions, quiet_, verbose_);

    if (!apply_) {
        size_t conflict_count =
            report_unresolved_conflicts(db, actions, local_manifest, remote_manifest);
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

    auto outcome = apply_actions(actions, local_root_, base_manifest, local_manifest,
                                 remote_manifest, remote_backend_, object_store_, on_conflict_);
    size_t conflict_count = report_unresolved_conflicts(db, actions, local_manifest, remote_manifest,
                                                        outcome.merged_paths);
    update_base_state_after_apply(db, actions, local_manifest, remote_manifest, outcome.failed_paths,
                                  outcome.merged_paths, local_root_, object_store_);

    std::unordered_set<std::string> markdown_base_hashes;
    for (const auto &[path, meta] : db.load_as_manifest()) {
        if (is_markdown_path(path)) {
            markdown_base_hashes.insert(meta.hash);
        }
    }
    object_store_.prune(markdown_base_hashes);

    cleanup_trash(local_root_ / ".trash", trash_retention_days_);

    if (!outcome.failed_paths.empty()) {
        std::cerr << outcome.failed_paths.size() << " action(s) failed and will be retried next run\n";
        return RunStatus::ActionsFailed;
    }

    return conflict_count > 0 ? RunStatus::ConflictsPending : RunStatus::Success;
}
