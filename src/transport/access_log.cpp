#include "access_log.hpp"

#include <nlohmann/json.hpp>

std::string format_access_log_entry(const AccessLogEntry &entry) {
    nlohmann::json j;
    j["ts"] = entry.timestamp;
    j["request_id"] = entry.request_id;
    j["method"] = entry.method;
    j["path"] = entry.path;
    j["status"] = entry.status;
    j["bytes"] = entry.response_bytes;
    j["duration_ms"] = entry.duration_ms;
    j["remote_addr"] = entry.remote_addr;
    return j.dump();
}
