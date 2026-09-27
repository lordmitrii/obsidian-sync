#include "config.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static Config parse(std::vector<std::string> args) {
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>("obsidian-sync"));

    for (auto &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }

    return parse_args(static_cast<int>(argv.size()), argv.data());
}

static bool throws_with(std::vector<std::string> args, const std::string &expected_message) {
    try {
        parse(std::move(args));
    } catch (const std::runtime_error &error) {
        return std::string(error.what()) == expected_message;
    }

    return false;
}

static void test_rejects_trailing_garbage_in_port() {
    expect(throws_with({"--server-root", "/tmp", "--server-port", "8080abc"},
                       "--server-port must be a number"),
           "port_trailing_garbage");
    expect(throws_with({"--server-root", "/tmp", "--server-port", "abc"},
                       "--server-port must be a number"),
           "port_non_numeric");
}

static void test_rejects_trailing_garbage_in_interval() {
    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--apply", "--watch",
                        "--interval", "30x"},
                       "--interval must be a number"),
           "interval_trailing_garbage");
}

static void test_accepts_valid_numbers() {
    Config config = parse({"--server-root", "/tmp", "--server-port", "8080"});
    expect(config.server_port == 8080, "port_accepts_plain_number");

    Config with_interval = parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--apply",
                                  "--watch", "--interval", "45"});
    expect(with_interval.watch_interval_seconds == 45, "interval_accepts_plain_number");
}

static void test_trusted_proxy() {
    Config config = parse({"--server-root", "/tmp", "--trusted-proxy", "127.0.0.1"});
    expect(config.trusted_proxy == "127.0.0.1", "trusted_proxy_is_captured");

    Config without = parse({"--server-root", "/tmp"});
    expect(without.trusted_proxy.empty(), "trusted_proxy_defaults_to_empty");

    expect(throws_with({"--server-root", "/tmp", "--trusted-proxy"},
                       "--trusted-proxy requires an IP address"),
           "trusted_proxy_requires_a_value");
}

static void test_insecure_http_and_cacert() {
    Config config = parse({"--local-root", "/tmp", "--remote-url", "http://example.com",
                           "--insecure-http", "--cacert", "/tmp/ca.pem"});
    expect(config.insecure_http, "insecure_http_flag_is_captured");
    expect(config.cacert_path == "/tmp/ca.pem", "cacert_path_is_captured");

    Config without = parse({"--local-root", "/tmp", "--remote-url", "https://example.com"});
    expect(!without.insecure_http, "insecure_http_defaults_to_false");
    expect(without.cacert_path.empty(), "cacert_path_defaults_to_empty");

    expect(throws_with({"--local-root", "/tmp", "--remote-url", "https://example.com", "--cacert"},
                       "--cacert requires a path"),
           "cacert_requires_a_value");
}

int main() {
    test_rejects_trailing_garbage_in_port();
    test_rejects_trailing_garbage_in_interval();
    test_accepts_valid_numbers();
    test_trusted_proxy();
    test_insecure_http_and_cacert();

    if (failures == 0) {
        std::cout << "All config tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
