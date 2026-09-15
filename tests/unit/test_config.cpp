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

int main() {
    test_rejects_trailing_garbage_in_port();
    test_rejects_trailing_garbage_in_interval();
    test_accepts_valid_numbers();

    if (failures == 0) {
        std::cout << "All config tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
