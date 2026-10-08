#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

struct MetricsSnapshot {
    std::uint64_t requests_total = 0;
    std::uint64_t response_bytes_total = 0;
    double uptime_seconds = 0.0;
    std::map<int, std::uint64_t> requests_by_status;
};

std::string format_metrics_prometheus(const MetricsSnapshot &snapshot);

class ServerMetrics {
public:
    void record_request(int status, std::size_t response_bytes);
    MetricsSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    std::uint64_t requests_total_ = 0;
    std::uint64_t response_bytes_total_ = 0;
    std::map<int, std::uint64_t> requests_by_status_;
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
};
