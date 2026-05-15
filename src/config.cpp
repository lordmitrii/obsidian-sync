#include "config.hpp"

#include <stdexcept>
#include <string>

Config parse_args(int argc, char *argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--vault") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--vault requires a path");
            }

            config.vault_path = argv[++i];
        } else if (arg == "--state") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--state requires a path");
            }

            config.state_db_path = argv[++i];

                  } else if (arg == "--dry-run") {
            config.dry_run = true;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (config.vault_path.empty()) {
        throw std::runtime_error("Missing required argument: --vault");
    }

    return config;
}