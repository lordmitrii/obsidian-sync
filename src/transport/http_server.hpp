#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

// trusted_proxy: when non-empty, a request whose peer address equals this
// value is rate-limited by its X-Real-IP header instead of its peer address
// (set by deploy/nginx.conf). Left empty, every request is keyed on its raw
// peer address, which behind a reverse proxy is always the proxy's own IP.
void run_http_server(const std::filesystem::path &server_root,
                     const std::string &host,
                     int port,
                     const std::string &bearer_token,
                     std::size_t max_upload_bytes,
                     int rate_limit_per_minute,
                     const std::string &trusted_proxy = "");
