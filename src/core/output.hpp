#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>

#include "filemeta.hpp"
#include "sync_plan.hpp"
#include "vault_status.hpp"
#include "version_history.hpp"

nlohmann::json manifest_to_json(const std::vector<FileMeta> &files);
void print_json_manifest(const std::vector<FileMeta> &files);
void print_text_actions(const std::vector<SyncAction> &actions);
void print_json_actions(const std::vector<SyncAction> &actions);
void write_json_manifest_to_file(const std::vector<FileMeta> &files,
                                 const std::string &output_path);

nlohmann::json vault_status_to_json(const VaultStatus &status);
void print_json_vault_status(const VaultStatus &status);
void print_text_vault_status(const VaultStatus &status);

// A conflicted note paired with the local/remote hashes and detection time
// recorded in state.db at the time the conflict was found, when available.
// detected_time is 0 and both hashes are empty for a conflict copy that
// exists on disk but was never recorded there (e.g. created outside a sync
// run).
struct ConflictDetail {
    std::string path;
    std::string local_hash;
    std::string remote_hash;
    std::int64_t detected_time = 0;
};

nlohmann::json conflicted_notes_to_json(const std::vector<ConflictDetail> &notes);
void print_json_conflicted_notes(const std::vector<ConflictDetail> &notes);
void print_text_conflicted_notes(const std::vector<ConflictDetail> &notes);

nlohmann::json file_versions_to_json(const std::vector<FileVersion> &versions);
void print_json_file_versions(const std::vector<FileVersion> &versions);
void print_text_file_versions(const std::vector<FileVersion> &versions);