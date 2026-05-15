#include "config.hpp"
#include "http_server.hpp"
#include "security.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    try {
        Config config = parse_args(argc, argv);

        if (!config.server_mode) {
            std::cerr << "Missing required argument: --server-root\n";
            return 1;
        }

        if (!config.local_root.empty() || !config.remote_root.empty() || !config.remote_url.empty()) {
            std::cerr << "Use obsidian-sync-client for client sync options\n";
            return 1;
        }

        if (!fs::exists(config.server_root) || !fs::is_directory(config.server_root)) {
            std::cerr << "Server root does not exist or is not a directory\n";
            return 1;
        }

        std::string token = load_required_bearer_token();
        run_http_server(config.server_root,
                        config.server_host,
                        config.server_port,
                        token,
                        DEFAULT_MAX_UPLOAD_BYTES,
                        DEFAULT_RATE_LIMIT_PER_MINUTE);

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
