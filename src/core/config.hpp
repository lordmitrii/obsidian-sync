#pragma once

#include "ignore_rules.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

struct Config {
    bool help = false;
    std::filesystem::path vault_path;
    std::string state_db_path = "state.db";
    bool dry_run = false;
    bool quiet = false;
    bool json_output = false;
    bool manifest_output = false;
    std::string manifest_file_path;
    std::filesystem::path server_root;
    std::string server_host = "127.0.0.1";
    int server_port = 38471;
    std::string trusted_proxy;
    bool server_mode = false;
    bool compare_manifests = false;
    std::string compare_manifest_a;
    std::string compare_manifest_b;
    std::filesystem::path local_root;
    std::filesystem::path remote_root;
    std::string remote_url;
    bool insecure_http = false;
    std::string cacert_path;
    bool apply = false;
    bool watch = false;
    int watch_interval_seconds = 30;
    bool force_deletes = false;
    size_t max_delete_count = 50;
    double max_delete_percent = 50.0;
    int trash_retention_days = 30;
    SyncConfigPreset sync_config = SyncConfigPreset::All;
};

Config parse_args(int argc, char *argv[]);
void print_usage();
