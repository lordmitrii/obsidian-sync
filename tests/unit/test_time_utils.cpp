#include "time_utils.hpp"

#include <chrono>
#include <iostream>
#include <regex>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static void test_format_timestamp_iso8601_is_deterministic() {
    auto epoch = std::chrono::system_clock::time_point(std::chrono::seconds(1700000000));
    expect(format_timestamp_iso8601(epoch) == "2023-11-14T22:13:20Z",
           "format_timestamp_iso8601_is_deterministic");
}

static void test_current_timestamp_iso8601_matches_shape() {
    static const std::regex pattern(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$)");
    expect(std::regex_match(current_timestamp_iso8601(), pattern),
           "current_timestamp_iso8601_matches_shape");
}

int main() {
    test_format_timestamp_iso8601_is_deterministic();
    test_current_timestamp_iso8601_matches_shape();

    if (failures == 0) {
        std::cout << "All time utils tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
