#include "metrics.hpp"

#include <iostream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

static void test_format_metrics_prometheus() {
    MetricsSnapshot snapshot;
    snapshot.requests_total = 42;
    snapshot.response_bytes_total = 12345;
    snapshot.uptime_seconds = 1.5;
    snapshot.requests_by_status[200] = 40;
    snapshot.requests_by_status[401] = 2;

    const std::string text = format_metrics_prometheus(snapshot);

    expect(contains(text, "obsidian_sync_requests_total 42"), "requests_total");
    expect(contains(text, "obsidian_sync_response_bytes_total 12345"), "response_bytes_total");
    expect(contains(text, "obsidian_sync_uptime_seconds 1.5"), "uptime_seconds");
    expect(contains(text, "obsidian_sync_requests_by_status_total{status=\"200\"} 40"), "status_200");
    expect(contains(text, "obsidian_sync_requests_by_status_total{status=\"401\"} 2"), "status_401");
}

static void test_format_metrics_prometheus_empty() {
    MetricsSnapshot snapshot;
    const std::string text = format_metrics_prometheus(snapshot);

    expect(contains(text, "obsidian_sync_requests_total 0"), "default_requests_total");
    expect(contains(text, "obsidian_sync_response_bytes_total 0"), "default_response_bytes_total");
    expect(!contains(text, "status=\""), "no_status_lines_when_empty");
}

static void test_server_metrics_records_requests() {
    ServerMetrics metrics;
    metrics.record_request(200, 100);
    metrics.record_request(200, 50);
    metrics.record_request(404, 10);

    MetricsSnapshot snapshot = metrics.snapshot();

    expect(snapshot.requests_total == 3, "records_total_count");
    expect(snapshot.response_bytes_total == 160, "sums_response_bytes");
    expect(snapshot.requests_by_status.at(200) == 2, "counts_status_200");
    expect(snapshot.requests_by_status.at(404) == 1, "counts_status_404");
}

int main() {
    test_format_metrics_prometheus();
    test_format_metrics_prometheus_empty();
    test_server_metrics_records_requests();

    if (failures == 0) {
        std::cout << "All metrics tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
