#pragma once

#include <filesystem>
#include <string>

void run_http_server(const std::filesystem::path &server_root,
                     const std::string &host,
                     int port);
