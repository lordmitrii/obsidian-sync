#include "db.hpp"
#include "scanner.hpp"

#include <filesystem>
#include <iostream>
#include <unordered_set>

#include "config.hpp"
#include "sync_plan.hpp"

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    Config config = parse_args(argc, argv);

    fs::path vault_path = config.vault_path;

    if (!fs::exists(vault_path)) {
        std::cerr << "Vault path does not exist\n";
        return 1;
    }

    if (!fs::is_directory(vault_path)) {
        std::cerr << "Vault path is not a directory\n";
        return 1;
    }

    try {
        Database db(config.state_db_path);
        db.initialize();

        auto files = scan_vault(vault_path);
        auto actions = build_sync_plan(db, files);

        for (const auto &action : actions) {
            std::cout << "[" << action_type_to_string(action.type) << "] " << action.path << "\n";
        }

        for (const auto &file : files) {
            db.save_file(file);
        }

        for (const auto &action : actions) {
            if (action.type == SyncActionType::Deleted) {
                db.delete_file(action.path);
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}