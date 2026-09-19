#include "hasher.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static fs::path write_temp(const std::string &name, const std::string &content) {
    const auto path = fs::temp_directory_path() / ("obsidian-sync-hasher-test-" + name);
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
}

static void test_known_digests() {
    const auto empty = write_temp("empty", "");
    const auto abc = write_temp("abc", "abc");

    expect(sha256_file(empty) ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "empty_file_digest");
    expect(sha256_file(abc) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "abc_digest");

    fs::remove(empty);
    fs::remove(abc);
}

static void test_larger_than_read_buffer() {
    // 8192 is the read buffer size; a file spanning several reads must hash the same
    // as the equivalent content hashed in one go.
    const auto big = write_temp("big", std::string(20000, 'a'));
    const auto same = write_temp("big-copy", std::string(20000, 'a'));
    const auto other = write_temp("big-other", std::string(19999, 'a'));

    expect(sha256_file(big) == sha256_file(same), "identical_content_same_digest");
    expect(sha256_file(big) != sha256_file(other), "different_content_different_digest");
    expect(sha256_file(big).size() == 64, "digest_is_64_hex_chars");

    fs::remove(big);
    fs::remove(same);
    fs::remove(other);
}

static void test_missing_file_throws() {
    bool threw = false;

    try {
        sha256_file(fs::temp_directory_path() / "obsidian-sync-hasher-test-missing");
    } catch (const std::runtime_error &) {
        threw = true;
    }

    expect(threw, "missing_file_throws");
}

int main() {
    test_known_digests();
    test_larger_than_read_buffer();
    test_missing_file_throws();

    if (failures == 0) {
        std::cout << "All hasher tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
