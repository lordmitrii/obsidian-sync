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

static void test_delete_safety_limits() {
    Config defaults = parse({"--local-root", "/tmp", "--remote-root", "/tmp"});
    expect(!defaults.force_deletes, "force_deletes_defaults_to_false");
    expect(defaults.max_delete_count == 50, "max_delete_count_defaults_to_50");
    expect(defaults.max_delete_percent == 50.0, "max_delete_percent_defaults_to_50");

    Config config = parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--force-deletes",
                           "--max-deletes", "10", "--max-delete-percent", "25.5"});
    expect(config.force_deletes, "force_deletes_flag_is_captured");
    expect(config.max_delete_count == 10, "max_delete_count_is_captured");
    expect(config.max_delete_percent == 25.5, "max_delete_percent_is_captured");

    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--max-deletes", "abc"},
                       "--max-deletes must be a number"),
           "max_deletes_rejects_non_numeric");
    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--max-delete-percent",
                        "150"},
                       "--max-delete-percent must be between 0 and 100"),
           "max_delete_percent_rejects_out_of_range");
}

static void test_trash_retention_days() {
    Config defaults = parse({"--local-root", "/tmp", "--remote-root", "/tmp"});
    expect(defaults.trash_retention_days == 30, "trash_retention_days_defaults_to_30");

    Config config =
        parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--trash-retention-days", "7"});
    expect(config.trash_retention_days == 7, "trash_retention_days_is_captured");

    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--trash-retention-days",
                        "abc"},
                       "--trash-retention-days must be a number"),
           "trash_retention_days_rejects_non_numeric");
}

static void test_sync_config_preset() {
    Config defaults = parse({"--local-root", "/tmp", "--remote-root", "/tmp"});
    expect(defaults.sync_config == SyncConfigPreset::All, "sync_config_defaults_to_all");

    Config none =
        parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--sync-config", "none"});
    expect(none.sync_config == SyncConfigPreset::None, "sync_config_accepts_none");

    Config core =
        parse({"--local-root", "/tmp", "--remote-root", "/tmp", "--sync-config", "core"});
    expect(core.sync_config == SyncConfigPreset::Core, "sync_config_accepts_core");

    expect(throws_with({"--local-root", "/tmp", "--remote-root", "/tmp", "--sync-config", "bogus"},
                       "--sync-config must be one of: none, core, all"),
           "sync_config_rejects_unknown_preset");
}

static void test_quiet() {
    Config defaults = parse({"--vault", "/tmp"});
    expect(!defaults.quiet, "quiet_defaults_to_false");

    Config quiet = parse({"--vault", "/tmp", "--quiet"});
    expect(quiet.quiet, "quiet_accepts_flag");
}

int main() {
    test_rejects_trailing_garbage_in_port();
    test_rejects_trailing_garbage_in_interval();
    test_accepts_valid_numbers();
    test_trusted_proxy();
    test_insecure_http_and_cacert();
    test_delete_safety_limits();
    test_trash_retention_days();
    test_sync_config_preset();
    test_quiet();

    if (failures == 0) {
        std::cout << "All config tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
