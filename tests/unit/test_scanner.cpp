#include "scanner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static void test_ignores_obsidian_noise() {
    expect(should_ignore("vault/.obsidian/workspace.json"), "workspace_json");
    expect(should_ignore("vault/.obsidian/workspace-mobile.json"), "workspace_mobile_json");
    expect(should_ignore("vault/.obsidian/cache/some-plugin-data"), "obsidian_cache");
    expect(should_ignore("vault/.trash/deleted-note.md"), "trash_dir");
    expect(should_ignore("vault/.DS_Store"), "ds_store");
    expect(should_ignore("vault/note.md.tmp"), "tmp_extension");
    expect(should_ignore("vault/note.md.swp"), "swp_extension");
    expect(should_ignore("vault/note.md.crswap"), "crswap_extension");
    expect(should_ignore("vault/note.conflict-remote.md"), "conflict_remote_marker");
}

static void test_does_not_ignore_real_notes() {
    expect(!should_ignore("vault/note.md"), "plain_note");
    expect(!should_ignore("vault/.obsidian/plugins/some-plugin/data.json"), "obsidian_plugin_data");
    expect(!should_ignore("vault/folder/subfolder/note.md"), "nested_note");
}

static void test_does_not_match_substrings() {
    expect(!should_ignore("vault/.obsidiantrash/note.md"), "not_obsidian_prefix_match");
    expect(!should_ignore("vault/cached-notes/note.md"), "not_cache_prefix_match");
    expect(!should_ignore("vault/mytrash/note.md"), "not_trash_prefix_match");
}

static void test_skips_symlinks() {
    fs::path root = fs::temp_directory_path() / "obsidian-sync-scanner-test-symlinks";
    fs::remove_all(root);
    fs::create_directories(root);

    fs::path target = root / "real.md";
    std::ofstream(target) << "content";

    fs::path link = root / "linked.md";
    std::error_code error;
    fs::create_symlink(target, link, error);

    if (error) {
        fs::remove_all(root);
        return;
    }

    expect(scan_file(root, "real.md").has_value(), "regular_file_is_scanned");
    expect(!scan_file(root, "linked.md").has_value(), "symlink_is_skipped");

    auto files = scan_vault(root);
    expect(files.size() == 1, "vault_scan_skips_symlink");

    fs::remove_all(root);
}

static void write_file(const fs::path &path, const std::string &content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

static fs::path fresh_dir(const std::string &name) {
    fs::path root = fs::temp_directory_path() / ("obsidian-sync-scanner-test-" + name);
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

static void test_reuses_cached_hash_when_size_and_mtime_match() {
    fs::path root = fresh_dir("cache-hit");
    fs::path note = root / "note.md";
    auto old_time = fs::file_time_type::clock::now() - std::chrono::hours(1);

    write_file(note, "aaaa");
    fs::last_write_time(note, old_time);
    auto original = scan_file(root, "note.md");
    expect(original.has_value(), "cache_hit_setup_scans_file");

    // Same size as "aaaa" and the same recorded mtime: a cache hit should
    // reuse the stale hash instead of rehashing this new content.
    write_file(note, "bbbb");
    fs::last_write_time(note, old_time);

    Manifest cache;
    cache[original->path] = *original;

    auto reused = scan_file(root, "note.md", cache);
    expect(reused.has_value() && reused->hash == original->hash,
           "cache_hit_reuses_stale_hash_when_size_and_mtime_match");

    fs::remove_all(root);
}

static void test_rehashes_when_mtime_differs_from_cache() {
    fs::path root = fresh_dir("cache-miss-mtime");
    fs::path note = root / "note.md";
    auto old_time = fs::file_time_type::clock::now() - std::chrono::hours(1);

    write_file(note, "aaaa");
    fs::last_write_time(note, old_time);
    auto original = scan_file(root, "note.md");
    expect(original.has_value(), "cache_miss_mtime_setup_scans_file");

    write_file(note, "bbbb");
    fs::last_write_time(note, old_time + std::chrono::seconds(10));

    Manifest cache;
    cache[original->path] = *original;

    auto result = scan_file(root, "note.md", cache);
    expect(result.has_value() && result->hash != original->hash,
           "cache_miss_on_mtime_change_rehashes");

    fs::remove_all(root);
}

static void test_rehashes_when_size_differs_from_cache() {
    fs::path root = fresh_dir("cache-miss-size");
    fs::path note = root / "note.md";
    auto old_time = fs::file_time_type::clock::now() - std::chrono::hours(1);

    write_file(note, "aaaa");
    fs::last_write_time(note, old_time);
    auto original = scan_file(root, "note.md");
    expect(original.has_value(), "cache_miss_size_setup_scans_file");

    write_file(note, "aaaaaaaa");
    fs::last_write_time(note, old_time);

    Manifest cache;
    cache[original->path] = *original;

    auto result = scan_file(root, "note.md", cache);
    expect(result.has_value() && result->hash != original->hash,
           "cache_miss_on_size_change_rehashes");

    fs::remove_all(root);
}

static void test_racy_mtime_ignores_cache() {
    fs::path root = fresh_dir("cache-racy-mtime");
    fs::path note = root / "note.md";

    write_file(note, "aaaa");
    auto fresh = scan_file(root, "note.md");
    expect(fresh.has_value(), "racy_mtime_setup_scans_file");

    FileMeta stale_but_matching = *fresh;
    stale_but_matching.hash = "not-the-real-hash";

    Manifest cache;
    cache[fresh->path] = stale_but_matching;

    auto result = scan_file(root, "note.md", cache);
    expect(result.has_value() && result->hash == fresh->hash,
           "racy_mtime_ignores_cache_and_rehashes");

    fs::remove_all(root);
}

int main() {
    test_ignores_obsidian_noise();
    test_does_not_ignore_real_notes();
    test_does_not_match_substrings();
    test_skips_symlinks();
    test_reuses_cached_hash_when_size_and_mtime_match();
    test_rehashes_when_mtime_differs_from_cache();
    test_rehashes_when_size_differs_from_cache();
    test_racy_mtime_ignores_cache();

    if (failures == 0) {
        std::cout << "All scanner tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
