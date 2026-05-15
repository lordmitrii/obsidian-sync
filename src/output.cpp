#include "output.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void print_text_actions(const std::vector<SyncAction>& actions) {
    for (const auto& action : actions) {
        std::cout << "[" << action_type_to_string(action.type) << "] "
                  << action.path << "\n";
    }
}

void print_json_actions(const std::vector<SyncAction>& actions) {
    json root;
    root["actions"] = json::array();

    for (const auto& action : actions) {
        root["actions"].push_back({
            {"type", action_type_to_string(action.type)},
            {"path", action.path}
        });
    }

    std::cout << root.dump(2) << "\n";
}