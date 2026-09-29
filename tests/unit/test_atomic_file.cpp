#include "atomic_file.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static void write_file(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

static std::string read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static fs::path fresh_dir(const std::string &name) {
    const auto dir = fs::temp_directory_path() / ("obsidian-sync-atomic-file-test-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static bool has_leftover_temp_files(const fs::path &dir) {
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (entry.path().string().find(".tmp-") != std::string::npos) {
            return true;
        }
    }

    return false;
}

static void test_write_creates_new_file() {
    const auto dir = fresh_dir("write-new");
    atomic_write_file(dir / "note.md", "hello");

    expect(read_file(dir / "note.md") == "hello", "write_creates_file_with_content");
    expect(!has_leftover_temp_files(dir), "write_leaves_no_temp_file");
    fs::remove_all(dir);
}

static void test_write_creates_parent_directories() {
    const auto dir = fresh_dir("write-nested");
    atomic_write_file(dir / "deep/nested/note.md", "hi");

    expect(read_file(dir / "deep/nested/note.md") == "hi", "write_creates_parent_dirs");
    fs::remove_all(dir);
}

static void test_write_fully_replaces_shorter_content() {
    const auto dir = fresh_dir("write-shrink");
    write_file(dir / "note.md", "a much longer original file body");

    atomic_write_file(dir / "note.md", "short");

    expect(read_file(dir / "note.md") == "short", "write_replaces_without_trailing_garbage");
    expect(!has_leftover_temp_files(dir), "overwrite_leaves_no_temp_file");
    fs::remove_all(dir);
}

static void test_copy_overwrites_destination() {
    const auto dir = fresh_dir("copy");
    write_file(dir / "source.md", "new content");
    write_file(dir / "dest.md", "stale content that is longer than the new one");

    atomic_copy_file(dir / "source.md", dir / "dest.md");

    expect(read_file(dir / "dest.md") == "new content", "copy_overwrites_destination");
    expect(!has_leftover_temp_files(dir), "copy_leaves_no_temp_file");
    fs::remove_all(dir);
}

static void test_copy_creates_parent_directories() {
    const auto dir = fresh_dir("copy-nested");
    write_file(dir / "source.md", "content");

    atomic_copy_file(dir / "source.md", dir / "deep/nested/dest.md");

    expect(read_file(dir / "deep/nested/dest.md") == "content", "copy_creates_parent_dirs");
    fs::remove_all(dir);
}

static void test_move_to_trash_moves_file() {
    const auto dir = fresh_dir("move-basic");
    write_file(dir / "vault/note.md", "v1");

    fs::path destination = move_to_trash(dir / "vault/note.md", dir / "trash", "note.md");

    expect(destination == dir / "trash/note.md", "move_to_trash_returns_destination");
    expect(!fs::exists(dir / "vault/note.md"), "move_to_trash_removes_source");
    expect(read_file(dir / "trash/note.md") == "v1", "move_to_trash_content_preserved");
    fs::remove_all(dir);
}

static void test_move_to_trash_keeps_relative_structure() {
    const auto dir = fresh_dir("move-nested");
    write_file(dir / "vault/notes/a.md", "v1");

    move_to_trash(dir / "vault/notes/a.md", dir / "trash", "notes/a.md");

    expect(read_file(dir / "trash/notes/a.md") == "v1", "move_to_trash_preserves_structure");
    fs::remove_all(dir);
}

static void test_move_to_trash_numbers_on_collision() {
    const auto dir = fresh_dir("move-collision");
    write_file(dir / "vault/note.md", "v1");
    move_to_trash(dir / "vault/note.md", dir / "trash", "note.md");

    write_file(dir / "vault/note.md", "v2");
    fs::path second = move_to_trash(dir / "vault/note.md", dir / "trash", "note.md");

    expect(read_file(dir / "trash/note.md") == "v1", "move_to_trash_first_copy_untouched");
    expect(read_file(second) == "v2", "move_to_trash_second_copy_gets_a_number");
    fs::remove_all(dir);
}

int main() {
    test_write_creates_new_file();
    test_write_creates_parent_directories();
    test_write_fully_replaces_shorter_content();
    test_copy_overwrites_destination();
    test_copy_creates_parent_directories();
    test_move_to_trash_moves_file();
    test_move_to_trash_keeps_relative_structure();
    test_move_to_trash_numbers_on_collision();

    if (failures != 0) {
        std::cerr << failures << " test case(s) failed\n";
        return 1;
    }

    std::cout << "All atomic file tests passed\n";
    return 0;
}
