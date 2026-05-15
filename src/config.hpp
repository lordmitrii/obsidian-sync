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
    bool server_mode = false;
};

Config parse_args(int argc, char *argv[]);