#include "access_log.hpp"

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

static void test_format_access_log_entry() {
    AccessLogEntry entry;
    entry.timestamp = "2026-10-02T00:00:00Z";
    entry.request_id = 42;
    entry.method = "GET";
    entry.path = "/manifest";
    entry.status = 200;
    entry.response_bytes = 1234;
    entry.duration_ms = 5.5;
    entry.remote_addr = "127.0.0.1";

    const std::string line = format_access_log_entry(entry);

    expect(contains(line, "\"ts\":\"2026-10-02T00:00:00Z\""), "timestamp_field");
    expect(contains(line, "\"request_id\":42"), "request_id_field");
    expect(contains(line, "\"method\":\"GET\""), "method_field");
    expect(contains(line, "\"path\":\"/manifest\""), "path_field");
    expect(contains(line, "\"status\":200"), "status_field");
    expect(contains(line, "\"bytes\":1234"), "bytes_field");
    expect(contains(line, "\"duration_ms\":5.5"), "duration_ms_field");
    expect(contains(line, "\"remote_addr\":\"127.0.0.1\""), "remote_addr_field");
}

static void test_format_access_log_entry_defaults() {
    AccessLogEntry entry;
    const std::string line = format_access_log_entry(entry);

    expect(contains(line, "\"request_id\":0"), "default_request_id");
    expect(contains(line, "\"status\":0"), "default_status");
    expect(contains(line, "\"bytes\":0"), "default_bytes");
}

int main() {
    test_format_access_log_entry();
    test_format_access_log_entry_defaults();

    if (failures == 0) {
        std::cout << "All access log tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
