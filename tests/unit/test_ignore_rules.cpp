#include "ignore_rules.hpp"

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

int main() {
    test_wildcard_matches_at_any_depth();
    test_anchored_pattern_only_matches_from_root();
    test_directory_only_pattern_matches_ancestors_not_files();
    test_comments_and_blank_lines_are_skipped();
    test_negation_lines_are_ignored();
    test_empty_rules_match_nothing();
    test_load_ignore_rules_reads_vault_root_file();
    test_load_ignore_rules_missing_file_is_empty();

    if (failures == 0) {
        std::cout << "All ignore rules tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
