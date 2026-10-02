#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct AccessLogEntry {
    std::string timestamp;
    std::uint64_t request_id = 0;
    std::string method;
    std::string path;
    int status = 0;
    std::size_t response_bytes = 0;
    double duration_ms = 0.0;
    std::string remote_addr;
};

std::string format_access_log_entry(const AccessLogEntry &entry);
