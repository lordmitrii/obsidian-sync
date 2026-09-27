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

static void test_is_secure_remote_url() {
    expect(is_secure_remote_url("https://sync.example.com"), "https_is_secure");
    expect(is_secure_remote_url("https://127.0.0.1:38471"), "https_loopback_is_secure");
    expect(is_secure_remote_url("http://127.0.0.1:38471"), "http_loopback_ip_is_secure");
    expect(is_secure_remote_url("http://localhost:38471"), "http_localhost_is_secure");
    expect(is_secure_remote_url("http://[::1]:38471"), "http_ipv6_loopback_is_secure");
    expect(!is_secure_remote_url("http://sync.example.com"), "plain_http_to_remote_host_is_insecure");
    expect(!is_secure_remote_url("http://127.0.0.1.evil.com"), "lookalike_host_is_insecure");
    expect(!is_secure_remote_url("ftp://127.0.0.1"), "unknown_scheme_is_insecure");
}

int main() {
    test_constant_time_equals();
    test_bearer_authorization_header();
    test_is_safe_relative_path();
    test_is_secure_remote_url();

    if (failures == 0) {
        std::cout << "All security tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
