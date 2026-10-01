#include "ignore_rules.hpp"

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

static void test_wildcard_matches_at_any_depth() {
    IgnoreRules rules;
    rules.add_line("*.pdf");

    expect(rules.matches("report.pdf"), "top_level_wildcard");
    expect(rules.matches("folder/report.pdf"), "nested_wildcard");
    expect(!rules.matches("report.md"), "wildcard_does_not_match_other_extension");
}

static void test_anchored_pattern_only_matches_from_root() {
    IgnoreRules rules;
    rules.add_line("/Attachments/large.bin");

    expect(rules.matches("Attachments/large.bin"), "anchored_match");
    expect(!rules.matches("sub/Attachments/large.bin"), "anchored_does_not_match_nested");
}

static void test_anchored_directory_pattern_matches_at_any_depth() {
    IgnoreRules rules;
    rules.add_line("/.obsidian/plugins/");

    expect(rules.matches(".obsidian/plugins/foo.json"), "anchored_dir_matches_direct_child");
    expect(rules.matches(".obsidian/plugins/some-plugin/data.json"),
          "anchored_dir_matches_nested_child");
    expect(!rules.matches(".obsidian/themes/foo.css"), "anchored_dir_does_not_match_sibling");
    expect(!rules.matches("plugins/foo.json"), "anchored_dir_does_not_match_unrooted_name");
}

static void test_directory_only_pattern_matches_ancestors_not_files() {
    IgnoreRules rules;
    rules.add_line("Excalidraw/");

    expect(rules.matches("Excalidraw/drawing.md"), "directory_only_matches_contents");
    expect(!rules.matches("Excalidraw"), "directory_only_does_not_match_same_name_file");
}

static void test_comments_and_blank_lines_are_skipped() {
    IgnoreRules rules;
    rules.add_line("# a comment");
    rules.add_line("");
    rules.add_line("   ");

    expect(rules.empty(), "comments_and_blanks_produce_no_patterns");
}

static void test_negation_lines_are_ignored() {
    IgnoreRules rules;
    rules.add_line("!keep.md");

    expect(rules.empty(), "negation_lines_are_not_added_as_patterns");
    expect(!rules.matches("keep.md"), "negation_line_does_not_match_anything");
}

static void test_empty_rules_match_nothing() {
    IgnoreRules rules;

    expect(!rules.matches("note.md"), "empty_rules_match_nothing");
}

static void test_load_ignore_rules_reads_vault_root_file() {
    fs::path root = fs::temp_directory_path() / "obsidian-sync-ignore-rules-test";
    fs::remove_all(root);
    fs::create_directories(root);

    std::ofstream(root / ".obsidian-sync-ignore") << "*.bak\n# comment\nArchive/\n";

    auto rules = load_ignore_rules(root);
    expect(rules.matches("note.md.bak"), "loaded_wildcard_pattern_matches");
    expect(rules.matches("Archive/old.md"), "loaded_directory_pattern_matches");
    expect(!rules.matches("note.md"), "loaded_rules_do_not_match_unrelated_file");

    fs::remove_all(root);
}

static void test_load_ignore_rules_missing_file_is_empty() {
    fs::path root = fs::temp_directory_path() / "obsidian-sync-ignore-rules-test-missing";
    fs::remove_all(root);
    fs::create_directories(root);

    auto rules = load_ignore_rules(root);
    expect(rules.empty(), "missing_ignore_file_yields_empty_rules");

    fs::remove_all(root);
}

static void test_parse_sync_config_preset() {
    expect(parse_sync_config_preset("all") == SyncConfigPreset::All, "parses_all");
    expect(parse_sync_config_preset("core") == SyncConfigPreset::Core, "parses_core");
    expect(parse_sync_config_preset("none") == SyncConfigPreset::None, "parses_none");

    bool threw = false;
    try {
        parse_sync_config_preset("bogus");
    } catch (const std::exception &) {
        threw = true;
    }
    expect(threw, "rejects_unknown_preset");
}

static void test_sync_config_preset_all_ignores_nothing_extra() {
    auto rules = ignore_rules_for_sync_config(SyncConfigPreset::All);
    expect(rules.empty(), "all_preset_adds_no_rules");
}

static void test_sync_config_preset_none_ignores_whole_obsidian_dir() {
    auto rules = ignore_rules_for_sync_config(SyncConfigPreset::None);

    expect(rules.matches(".obsidian/app.json"), "none_preset_ignores_top_level_config");
    expect(rules.matches(".obsidian/plugins/some-plugin/data.json"), "none_preset_ignores_nested");
    expect(!rules.matches("note.md"), "none_preset_does_not_ignore_notes");
}

static void test_sync_config_preset_core_keeps_settings_drops_payloads() {
    auto rules = ignore_rules_for_sync_config(SyncConfigPreset::Core);

    expect(!rules.matches(".obsidian/app.json"), "core_preset_keeps_app_json");
    expect(!rules.matches(".obsidian/hotkeys.json"), "core_preset_keeps_hotkeys");
    expect(rules.matches(".obsidian/plugins/some-plugin/data.json"), "core_preset_drops_plugins");
    expect(rules.matches(".obsidian/themes/theme.css"), "core_preset_drops_themes");
    expect(rules.matches(".obsidian/snippets/snippet.css"), "core_preset_drops_snippets");
    expect(rules.matches(".obsidian/icons/pack/icon.svg"), "core_preset_drops_icons");
}

static void test_merge_combines_two_rule_sets() {
    IgnoreRules a;
    a.add_line("*.bak");
    IgnoreRules b;
    b.add_line("Archive/");

    a.merge(b);

    expect(a.matches("note.md.bak"), "merge_keeps_original_rules");
    expect(a.matches("Archive/old.md"), "merge_adds_other_rules");
}

int main() {
    test_wildcard_matches_at_any_depth();
    test_anchored_pattern_only_matches_from_root();
    test_anchored_directory_pattern_matches_at_any_depth();
    test_directory_only_pattern_matches_ancestors_not_files();
    test_comments_and_blank_lines_are_skipped();
    test_negation_lines_are_ignored();
    test_empty_rules_match_nothing();
    test_load_ignore_rules_reads_vault_root_file();
    test_load_ignore_rules_missing_file_is_empty();
    test_parse_sync_config_preset();
    test_sync_config_preset_all_ignores_nothing_extra();
    test_sync_config_preset_none_ignores_whole_obsidian_dir();
    test_sync_config_preset_core_keeps_settings_drops_payloads();
    test_merge_combines_two_rule_sets();

    if (failures == 0) {
        std::cout << "All ignore rules tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
