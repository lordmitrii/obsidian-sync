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
        } else if (arg == "--json") {
            config.json_output = true;
        } else if (arg == "--manifest") {
            config.manifest_output = true;
        } else if (arg == "--manifest-file") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--manifest-file requires a path");
            }

            config.manifest_file_path = argv[++i];
        } else if (arg == "--server-root") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--server-root requires a path");
            }

            config.server_root = argv[++i];
            config.server_mode = true;
        } else if (arg == "--server-host") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--server-host requires a host");
            }

            config.server_host = argv[++i];
        } else if (arg == "--server-port") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--server-port requires a port");
            }

            config.server_port = std::stoi(argv[++i]);

            if (config.server_port <= 0 || config.server_port > 65535) {
                throw std::runtime_error("--server-port must be between 1 and 65535");
            }
        } else if (arg == "--compare-manifests") {
            if (i + 2 >= argc) {
                throw std::runtime_error("--compare-manifests requires two manifest paths");
            }

            config.compare_manifests = true;

            config.compare_manifest_a = argv[++i];
            config.compare_manifest_b = argv[++i];
        } else if (arg == "--local-root") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--local-root requires a path");
            }

            config.local_root = argv[++i];

        } else if (arg == "--remote-root") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--remote-root requires a path");
            }

            config.remote_root = argv[++i];

        } else if (arg == "--apply") {
            config.apply = true;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (!config.compare_manifests && config.vault_path.empty() && config.server_root.empty() &&
        (config.local_root.empty() || config.remote_root.empty())) {
        throw std::runtime_error("Missing required argument: --vault, --server-root, "
                                 "--compare-manifests, or --local-root with --remote-root");
    }
    return config;
}
