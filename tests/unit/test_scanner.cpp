#include "scanner.hpp"

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

int main() {
    test_ignores_obsidian_noise();
    test_does_not_ignore_real_notes();
    test_does_not_match_substrings();
    test_skips_symlinks();

    if (failures == 0) {
        std::cout << "All scanner tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
