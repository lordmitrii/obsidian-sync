#include "manifest.hpp"
#include "output.hpp"

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

static fs::path temp_path(const std::string &name) {
    return fs::temp_directory_path() / ("obsidian-sync-manifest-test-" + name);
}

static void write_text(const fs::path &path, const std::string &text) {
    std::ofstream out(path);
    out << text;
}

static bool load_fails_mentioning(const fs::path &path) {
    try {
        load_manifest(path.string());
    } catch (const std::runtime_error &error) {
        return std::string(error.what()).find(path.string()) != std::string::npos;
    }

    return false;
}

static void test_round_trip() {
    const auto path = temp_path("round-trip.json");

    std::vector<FileMeta> files = {{"a.md", "hash-a", 12, 1700000000},
                                   {"dir/b.md", "hash-b", 0, 1700000100}};
    write_json_manifest_to_file(files, path.string());

    Manifest loaded = load_manifest(path.string());
    fs::remove(path);

    expect(loaded.size() == 2, "round_trip_size");
    expect(loaded.at("a.md").hash == "hash-a", "round_trip_hash");
    expect(loaded.at("a.md").size == 12, "round_trip_size_field");
    expect(loaded.at("dir/b.md").modified_time == 1700000100, "round_trip_modified_time");
}

static void test_empty_manifest() {
    const auto path = temp_path("empty.json");

    write_text(path, R"({"files": []})");
    expect(load_manifest(path.string()).empty(), "empty_files_array");

    fs::remove(path);
}

static void test_errors_mention_the_path() {
    const auto missing = temp_path("does-not-exist.json");
    fs::remove(missing);
    expect(load_fails_mentioning(missing), "missing_file");

    const auto broken = temp_path("broken.json");
    write_text(broken, "{ not json");
    expect(load_fails_mentioning(broken), "invalid_json");

    write_text(broken, R"({"files": [{"path": "a.md"}]})");
    expect(load_fails_mentioning(broken), "missing_fields");

    fs::remove(broken);
}

static bool parse_json_fails(const std::string &json_text) {
    try {
        parse_manifest_json(json_text);
    } catch (const std::runtime_error &) {
        return true;
    }

    return false;
}

static void test_rejects_unsafe_paths() {
    expect(parse_json_fails(R"({"files": [
        {"path": "../secrets.md", "hash": "h", "size": 1, "modified_time": 1}
    ]})"),
           "rejects_dotdot_path");

    expect(parse_json_fails(R"({"files": [
        {"path": "/etc/passwd", "hash": "h", "size": 1, "modified_time": 1}
    ]})"),
           "rejects_absolute_path");

    expect(!parse_json_fails(R"({"files": [
        {"path": "notes/a.md", "hash": "h", "size": 1, "modified_time": 1}
    ]})"),
           "accepts_safe_path");
}

int main() {
    test_round_trip();
    test_empty_manifest();
    test_errors_mention_the_path();
    test_rejects_unsafe_paths();

    if (failures == 0) {
        std::cout << "All manifest tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
