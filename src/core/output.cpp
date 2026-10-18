#include "output.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void print_text_actions(const std::vector<SyncAction> &actions) {
    for (const auto &action : actions) {
        std::cout << "[" << action_type_to_string(action.type) << "] " << action.path << "\n";
    }
}

void print_json_actions(const std::vector<SyncAction> &actions) {
    json root;
    root["actions"] = json::array();

    for (const auto &action : actions) {
        root["actions"].push_back(
            {{"type", action_type_to_string(action.type)}, {"path", action.path}});
    }

    std::cout << root.dump(2) << "\n";
}

json manifest_to_json(const std::vector<FileMeta> &files) {
    json root;
    root["files"] = json::array();

    for (const auto &file : files) {
        root["files"].push_back({{"path", file.path},
                                 {"hash", file.hash},
                                 {"size", file.size},
                                 {"modified_time", file.modified_time}});
    }

    return root;
}

void print_json_manifest(const std::vector<FileMeta> &files) {
    std::cout << manifest_to_json(files).dump(2) << "\n";
}

void write_json_manifest_to_file(const std::vector<FileMeta> &files,
                                 const std::string &output_path) {
    std::ofstream out(output_path);

    if (!out) {
        throw std::runtime_error("Could not open manifest file for writing");
    }

    out << manifest_to_json(files).dump(2) << "\n";
}

json vault_status_to_json(const VaultStatus &status) {
    return json{{"tracked_files", status.tracked_files},
               {"tracked_bytes", status.tracked_bytes},
               {"unresolved_conflicts", status.unresolved_conflicts},
               {"trashed_files", status.trashed_files}};
}

void print_json_vault_status(const VaultStatus &status) {
    std::cout << vault_status_to_json(status).dump(2) << "\n";
}

void print_text_vault_status(const VaultStatus &status) {
    std::cout << "Tracked files: " << status.tracked_files << "\n";
    std::cout << "Tracked bytes: " << status.tracked_bytes << "\n";
    std::cout << "Unresolved conflicts: " << status.unresolved_conflicts << "\n";
    std::cout << "Trashed files: " << status.trashed_files << "\n";
}

json conflicted_notes_to_json(const std::vector<ConflictDetail> &notes) {
    json root;
    root["conflicts"] = json::array();

    for (const auto &note : notes) {
        root["conflicts"].push_back({{"path", note.path},
                                     {"local_hash", note.local_hash},
                                     {"remote_hash", note.remote_hash},
                                     {"detected_time", note.detected_time}});
    }

    return root;
}

void print_json_conflicted_notes(const std::vector<ConflictDetail> &notes) {
    std::cout << conflicted_notes_to_json(notes).dump(2) << "\n";
}

void print_text_conflicted_notes(const std::vector<ConflictDetail> &notes) {
    if (notes.empty()) {
        std::cout << "No unresolved conflicts\n";
        return;
    }

    for (const auto &note : notes) {
        std::cout << note.path;

        if (!note.local_hash.empty() || !note.remote_hash.empty()) {
            std::cout << "  local=" << note.local_hash << " remote=" << note.remote_hash
                      << " detected=" << note.detected_time;
        }

        std::cout << "\n";
    }
}