#include "config.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

void print_usage() {
    std::cout <<
        "obsidian-sync client/server\n"
        "\n"
        "Client usage:\n"
        "  obsidian-sync-client --vault <path> [--dry-run] [--json]\n"
        "  obsidian-sync-client --vault <path> --manifest | --manifest-file <path>\n"
        "  obsidian-sync-client --compare-manifests <a> <b>\n"
        "  obsidian-sync-client --local-root <path> (--remote-root <path> | --remote-url <url>)\n"
        "                        [--state <path>] [--apply] [--watch --interval <seconds>]\n"
        "\n"
        "Server usage:\n"
        "  obsidian-sync-server --server-root <path> [--server-host <host>] [--server-port <port>]\n"
        "\n"
        "Options:\n"
        "  --vault <path>              Vault directory to scan\n"
        "  --state <path>              State database path (default: state.db)\n"
        "  --dry-run                   Compute the plan without touching the database\n"
        "  --json                      Print output as JSON\n"
        "  --manifest                  Print a manifest of the vault instead of a sync plan\n"
        "  --manifest-file <path>      Write a manifest of the vault to a file\n"
        "  --compare-manifests <a> <b> Diff two saved manifests\n"
        "  --local-root <path>         Local vault to sync\n"
        "  --remote-root <path>        Remote directory to sync against (for testing)\n"
        "  --remote-url <url>          Remote HTTP server to sync against\n"
        "  --apply                     Execute the sync plan\n"
        "  --watch                     Keep syncing on an interval (requires --apply)\n"
        "  --interval <seconds>        Seconds between watch runs (default: 30)\n"
        "  --server-root <path>        Directory to serve\n"
        "  --server-host <host>        Address to bind (default: 127.0.0.1)\n"
        "  --server-port <port>        Port to bind (default: 38471)\n"
        "  --help, -h                  Show this message\n";
}

Config parse_args(int argc, char *argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            config.help = true;
            return config;
        } else if (arg == "--vault") {
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

            try {
                config.server_port = std::stoi(argv[i + 1]);
            } catch (const std::exception &) {
                throw std::runtime_error("--server-port must be a number");
            }
            ++i;

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

        } else if (arg == "--remote-url") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--remote-url requires a URL");
            }

            config.remote_url = argv[++i];

        } else if (arg == "--apply") {
            config.apply = true;
        } else if (arg == "--watch") {
            config.watch = true;
        } else if (arg == "--interval") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--interval requires seconds");
            }

            try {
                config.watch_interval_seconds = std::stoi(argv[i + 1]);
            } catch (const std::exception &) {
                throw std::runtime_error("--interval must be a number");
            }
            ++i;

            if (config.watch_interval_seconds <= 0) {
                throw std::runtime_error("--interval must be greater than zero");
            }
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (config.watch && !config.apply) {
        throw std::runtime_error("--watch requires --apply");
    }

    if (!config.compare_manifests && config.vault_path.empty() && config.server_root.empty() &&
        (config.local_root.empty() || (config.remote_root.empty() && config.remote_url.empty()))) {
        throw std::runtime_error("Missing required argument: --vault, --server-root, "
                                 "--compare-manifests, or --local-root with --remote-root/--remote-url");
    }
    return config;
}
