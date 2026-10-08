#include "metrics.hpp"

#include <sstream>

void ServerMetrics::record_request(int status, std::size_t response_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++requests_total_;
    response_bytes_total_ += response_bytes;
    ++requests_by_status_[status];
}

MetricsSnapshot ServerMetrics::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    MetricsSnapshot snapshot;
    snapshot.requests_total = requests_total_;
    snapshot.response_bytes_total = response_bytes_total_;
    snapshot.requests_by_status = requests_by_status_;
    snapshot.uptime_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
    return snapshot;
}

std::string format_metrics_prometheus(const MetricsSnapshot &snapshot) {
    std::ostringstream out;
    out << "# HELP obsidian_sync_requests_total Total HTTP requests served\n";
    out << "# TYPE obsidian_sync_requests_total counter\n";
    out << "obsidian_sync_requests_total " << snapshot.requests_total << "\n";

    out << "# HELP obsidian_sync_response_bytes_total Total response bytes sent\n";
    out << "# TYPE obsidian_sync_response_bytes_total counter\n";
    out << "obsidian_sync_response_bytes_total " << snapshot.response_bytes_total << "\n";

    out << "# HELP obsidian_sync_uptime_seconds Seconds since the server started\n";
    out << "# TYPE obsidian_sync_uptime_seconds gauge\n";
    out << "obsidian_sync_uptime_seconds " << snapshot.uptime_seconds << "\n";

    out << "# HELP obsidian_sync_requests_by_status_total Requests served by HTTP status code\n";
    out << "# TYPE obsidian_sync_requests_by_status_total counter\n";
    for (const auto &[status, count] : snapshot.requests_by_status) {
        out << "obsidian_sync_requests_by_status_total{status=\"" << status << "\"} " << count << "\n";
    }

    return out.str();
}
