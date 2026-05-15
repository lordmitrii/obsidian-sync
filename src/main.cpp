#include "db.hpp"
#include "scanner.hpp"

#include <iostream>

#include "config.hpp"
#include "output.hpp"
#include "sync_plan.hpp"

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    Config config = parse_args(argc, argv);

    fs::path root_path = config.server_mode ? config.server_root : config.vault_path;

    if (!fs::exists(root_path)) {
        std::cerr << "Path does not exist\n";
        return 1;
    }

    if (!fs::is_directory(root_path)) {
        std::cerr << "Path is not a directory\n";
        return 1;
    }

    try {
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