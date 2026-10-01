#pragma once

#include "ignore_rules.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

void run_http_server(const std::filesystem::path &server_root,
                     const std::string &host,
                     int port,
                     const std::string &bearer_token,
                     std::size_t max_upload_bytes,
                     int rate_limit_per_minute,
                     const std::string &trusted_proxy = "",
                     int trash_retention_days = 30,
                     SyncConfigPreset sync_config = SyncConfigPreset::All);
