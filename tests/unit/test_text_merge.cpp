#include "text_merge.hpp"

#include <iostream>
#include <random>
#include <string>
#include <vector>

static int failures = 0;

static void expect(bool condition, const std::string &case_name) {
    if (!condition) {
        std::cerr << "FAIL [" << case_name << "]\n";
        ++failures;
    }
}

static void expect_clean(const MergeResult &result, const std::string &expected_content,
                         const std::string &case_name) {
    expect(result.status == MergeStatus::Clean, case_name + " (expected clean)");
    expect(result.content == expected_content, case_name + " (content)");
}

static void expect_conflicted(const MergeResult &result, const std::string &case_name) {
    expect(result.status == MergeStatus::Conflicted, case_name + " (expected conflict)");
    expect(result.content.find("<<<<<<< local\n") != std::string::npos,
          case_name + " (local marker)");
    expect(result.content.find("=======\n") != std::string::npos, case_name + " (split marker)");
    expect(result.content.find(">>>>>>> remote\n") != std::string::npos,
          case_name + " (remote marker)");
}

static void test_identity() {
    for (const std::string &text : {std::string(""), std::string("one line, no newline"),
                                     std::string("a\nb\nc\n"), std::string("a\nb\nc")}) {
        expect_clean(merge_three_way_text(text, text, text), text, "identity");
    }
}

static void test_only_ours_changed() {
    std::string base = "a\nb\nc\n";
    std::string ours = "a\nX\nc\n";
    expect_clean(merge_three_way_text(base, ours, base), ours, "only_ours_changed");
}

static void test_only_theirs_changed() {
    std::string base = "a\nb\nc\n";
    std::string theirs = "a\nb\nY\n";
    expect_clean(merge_three_way_text(base, base, theirs), theirs, "only_theirs_changed");
}

static void test_same_edit_both_sides_is_clean() {
    std::string base = "a\nb\nc\n";
    std::string edited = "a\nchanged\nc\n";
    expect_clean(merge_three_way_text(base, edited, edited), edited, "same_edit_both_sides");
}

static void test_conflicting_edit_same_line() {
    std::string base = "a\nb\nc\n";
    std::string ours = "a\nours\nc\n";
    std::string theirs = "a\ntheirs\nc\n";
    expect_conflicted(merge_three_way_text(base, ours, theirs), "conflicting_edit_same_line");
}

static void test_independent_edits_both_apply() {
    std::string base = "1\n2\n3\n4\n5\n";
    std::string ours = "ONE\n2\n3\n4\n5\n";
    std::string theirs = "1\n2\n3\n4\nFIVE\n";
    expect_clean(merge_three_way_text(base, ours, theirs), "ONE\n2\n3\n4\nFIVE\n",
                "independent_edits_both_apply");
}

static void test_insertions_at_different_points_both_apply() {
    std::string base = "a\nb\n";
    std::string ours = "inserted\na\nb\n";
    std::string theirs = "a\nb\nappended\n";
    expect_clean(merge_three_way_text(base, ours, theirs), "inserted\na\nb\nappended\n",
                "insertions_at_different_points");
}

static void test_conflicting_insertions_at_same_point() {
    std::string base = "a\nb\n";
    std::string ours = "a\nours-insert\nb\n";
    std::string theirs = "a\ntheirs-insert\nb\n";
    expect_conflicted(merge_three_way_text(base, ours, theirs),
                      "conflicting_insertions_at_same_point");
}

static void test_deletion_vs_edit_conflicts() {
    std::string base = "a\nb\nc\n";
    std::string ours = "a\nc\n";
    std::string theirs = "a\nedited\nc\n";
    expect_conflicted(merge_three_way_text(base, ours, theirs), "deletion_vs_edit");
}

static void test_no_trailing_newline_is_preserved() {
    std::string base = "a\nb";
    std::string ours = "a\nchanged";
    expect_clean(merge_three_way_text(base, ours, base), "a\nchanged",
                "no_trailing_newline_preserved");
}

static void test_both_sides_delete_everything_is_clean() {
    std::string base = "a\nb\nc\n";
    expect_clean(merge_three_way_text(base, "", ""), "", "both_delete_everything");
}

static void test_swap_symmetry_property() {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> line_count(1, 6);
    std::uniform_int_distribution<int> mutate(0, 3);

    for (int trial = 0; trial < 200; ++trial) {
        int n = line_count(rng);
        std::string base;
        for (int i = 0; i < n; ++i) {
            base += "line" + std::to_string(i) + "\n";
        }

        auto mutate_copy = [&](const std::string &text) {
            std::vector<std::string> lines;
            size_t start = 0;
            while (start < text.size()) {
                size_t pos = text.find('\n', start);
                lines.push_back(text.substr(start, pos - start));
                start = pos + 1;
            }

            if (!lines.empty() && mutate(rng) == 0) {
                lines[static_cast<size_t>(mutate(rng)) % lines.size()] += "-edited";
            }

            std::string out;
            for (const auto &line : lines) {
                out += line + "\n";
            }
            return out;
        };

        std::string ours = mutate_copy(base);
        std::string theirs = mutate_copy(base);

        auto forward = merge_three_way_text(base, ours, theirs);
        auto swapped = merge_three_way_text(base, theirs, ours);

        expect(forward.status == swapped.status, "swap_symmetry_status");

        if (forward.status == MergeStatus::Clean) {
            expect(forward.content == swapped.content, "swap_symmetry_clean_content");
        }
    }
}

int main() {
    test_identity();
    test_only_ours_changed();
    test_only_theirs_changed();
    test_same_edit_both_sides_is_clean();
    test_conflicting_edit_same_line();
    test_independent_edits_both_apply();
    test_insertions_at_different_points_both_apply();
    test_conflicting_insertions_at_same_point();
    test_deletion_vs_edit_conflicts();
    test_no_trailing_newline_is_preserved();
    test_both_sides_delete_everything_is_clean();
    test_swap_symmetry_property();

    if (failures == 0) {
        std::cout << "All text merge tests passed\n";
        return 0;
    }

    std::cerr << failures << " test case(s) failed\n";
    return 1;
}
