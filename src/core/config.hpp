#pragma once

#include <filesystem>
#include <string>

struct Config {
    std::filesystem::path vault_path;
    std::string state_db_path = "state.db";
    bool dry_run = false;
    bool json_output = false;
    bool manifest_output = false;
    std::string manifest_file_path;
    std::filesystem::path server_root;
    std::string server_host = "127.0.0.1";
    int server_port = 8080;
    bool server_mode = false;
    bool compare_manifests = false;
    std::string compare_manifest_a;
    std::string compare_manifest_b;
    std::filesystem::path local_root;
    std::filesystem::path remote_root;
    std::string remote_url;
    bool apply = false;
    bool watch = false;
    int watch_interval_seconds = 30;
};

Config parse_args(int argc, char *argv[]);
