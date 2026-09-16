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