#include "security.hpp"

#include <iostream>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static void test_constant_time_equals() {
    expect(constant_time_equals("secret", "secret"), "equal_strings_match");
    expect(!constant_time_equals("secret", "wrong!"), "different_strings_of_same_length");
    expect(!constant_time_equals("secret", "secre"), "different_lengths");
    expect(constant_time_equals("", ""), "empty_strings_match");
}

static void test_bearer_authorization_header() {
    expect(bearer_authorization_header("abc123") == "Bearer abc123", "formats_bearer_header");
}

static void test_is_safe_relative_path() {
    expect(is_safe_relative_path("notes/todo.md"), "plain_relative_path");
    expect(is_safe_relative_path("todo.md"), "top_level_file");
    expect(!is_safe_relative_path(""), "empty_path");
    expect(!is_safe_relative_path("../secrets.md"), "leading_dotdot");
    expect(!is_safe_relative_path("notes/../../secrets.md"), "embedded_dotdot");
    expect(!is_safe_relative_path("/etc/passwd"), "absolute_path");
}

int main() {
    test_constant_time_equals();
    test_bearer_authorization_header();
    test_is_safe_relative_path();

    if (failures == 0) {
        std::cout << "All security tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
