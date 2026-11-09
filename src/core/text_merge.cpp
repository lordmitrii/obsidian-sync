#include "text_merge.hpp"

#include <algorithm>
#include <vector>

namespace {

std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    size_t start = 0;

    while (start < text.size()) {
        size_t pos = text.find('\n', start);

        if (pos == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }

        lines.push_back(text.substr(start, pos - start + 1));
        start = pos + 1;
    }

    return lines;
}

struct ChangeOp {
    size_t base_start, base_end;
    size_t other_start, other_end;
};

// Classic LCS-based line diff, reduced to just the changed regions (equal
// runs are the gaps between them and never need to be materialized here).
std::vector<ChangeOp> diff_change_ops(const std::vector<std::string> &base,
                                      const std::vector<std::string> &other) {
    size_t n = base.size();
    size_t m = other.size();
    std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));

    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            lcs[i][j] = (base[i] == other[j]) ? lcs[i + 1][j + 1] + 1
                                               : std::max(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }

    std::vector<ChangeOp> ops;
    size_t i = 0, j = 0;
    bool in_change = false;
    size_t change_base_start = 0, change_other_start = 0;

    auto close_change = [&](size_t base_end, size_t other_end) {
        if (in_change) {
            ops.push_back({change_base_start, base_end, change_other_start, other_end});
            in_change = false;
        }
    };
    auto open_change = [&](size_t base_pos, size_t other_pos) {
        if (!in_change) {
            in_change = true;
            change_base_start = base_pos;
            change_other_start = other_pos;
        }
    };

    while (i < n && j < m) {
        if (base[i] == other[j] && lcs[i][j] == lcs[i + 1][j + 1] + 1) {
            close_change(i, j);
            ++i;
            ++j;
        } else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
            open_change(i, j);
            ++i;
        } else {
            open_change(i, j);
            ++j;
        }
    }

    if (i < n) {
        open_change(i, j);
        i = n;
    }

    if (j < m) {
        open_change(i, j);
        j = m;
    }

    close_change(i, j);

    return ops;
}

enum class Side { Ours, Theirs };

struct Hunk {
    Side side;
    size_t base_start, base_end;
    std::string text;
};

std::vector<Hunk> collect_hunks(const std::vector<std::string> &base,
                                const std::vector<std::string> &other,
                                Side side) {
    std::vector<Hunk> hunks;

    for (const auto &op : diff_change_ops(base, other)) {
        std::string text;

        for (size_t k = op.other_start; k < op.other_end; ++k) {
            text += other[k];
        }

        hunks.push_back({side, op.base_start, op.base_end, std::move(text)});
    }

    return hunks;
}

// Two hunks are genuinely in conflict if they cover (at least partly) the
// same base lines. A pure insertion is zero-width, so it only overlaps
// another zero-width insertion at the exact same point (both sides trying
// to insert something right there, order undefined) or a non-empty range
// that strictly contains that point.
bool genuinely_overlaps(const Hunk &a, const Hunk &b) {
    if (a.base_start == a.base_end && b.base_start == b.base_end) {
        return a.base_start == b.base_start;
    }

    return a.base_start < b.base_end && b.base_start < a.base_end;
}

std::string ensure_trailing_newline(std::string text) {
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }

    return text;
}

} // namespace

MergeResult merge_three_way_text(const std::string &base_text,
                                 const std::string &ours_text,
                                 const std::string &theirs_text) {
    std::vector<std::string> base = split_lines(base_text);
    std::vector<std::string> ours = split_lines(ours_text);
    std::vector<std::string> theirs = split_lines(theirs_text);

    auto base_range = [&](size_t start, size_t end) {
        std::string text;

        for (size_t k = start; k < end; ++k) {
            text += base[k];
        }

        return text;
    };

    // One side's full text over [cluster_start, cluster_end): that side's
    // hunks where it touched the range, base content everywhere it didn't.
    auto reconstruct_side = [&](Side side,
                                const std::vector<Hunk> &all_hunks,
                                const std::vector<size_t> &members,
                                size_t cluster_start,
                                size_t cluster_end) {
        std::string text;
        size_t pos = cluster_start;

        for (size_t member : members) {
            const Hunk &hunk = all_hunks[member];

            if (hunk.side != side) {
                continue;
            }

            if (hunk.base_start > pos) {
                text += base_range(pos, hunk.base_start);
            }

            text += hunk.text;
            pos = std::max(pos, hunk.base_end);
        }

        if (pos < cluster_end) {
            text += base_range(pos, cluster_end);
        }

        return text;
    };

    std::vector<Hunk> all_hunks = collect_hunks(base, ours, Side::Ours);
    for (auto &hunk : collect_hunks(base, theirs, Side::Theirs)) {
        all_hunks.push_back(std::move(hunk));
    }

    std::sort(all_hunks.begin(), all_hunks.end(), [](const Hunk &a, const Hunk &b) {
        if (a.base_start != b.base_start) {
            return a.base_start < b.base_start;
        }

        return a.base_end < b.base_end;
    });

    MergeResult result;
    std::string &content = result.content;
    size_t cursor = 0;
    size_t idx = 0;

    while (idx < all_hunks.size()) {
        size_t cluster_start = all_hunks[idx].base_start;
        size_t cluster_end = all_hunks[idx].base_end;
        std::vector<size_t> members{idx};
        size_t next = idx + 1;

        while (next < all_hunks.size() && all_hunks[next].base_start <= cluster_end) {
            cluster_end = std::max(cluster_end, all_hunks[next].base_end);
            members.push_back(next);
            ++next;
        }

        if (cluster_start > cursor) {
            content += base_range(cursor, cluster_start);
        }

        bool overlap_found = false;

        for (size_t a = 0; a < members.size() && !overlap_found; ++a) {
            for (size_t b = a + 1; b < members.size() && !overlap_found; ++b) {
                const Hunk &ha = all_hunks[members[a]];
                const Hunk &hb = all_hunks[members[b]];

                if (ha.side != hb.side && genuinely_overlaps(ha, hb)) {
                    overlap_found = true;
                }
            }
        }

        if (!overlap_found) {
            size_t pos = cluster_start;

            for (size_t member : members) {
                const Hunk &hunk = all_hunks[member];

                if (hunk.base_start > pos) {
                    content += base_range(pos, hunk.base_start);
                }

                content += hunk.text;
                pos = std::max(pos, hunk.base_end);
            }

            if (pos < cluster_end) {
                content += base_range(pos, cluster_end);
            }
        } else {
            std::string ours_full =
                reconstruct_side(Side::Ours, all_hunks, members, cluster_start, cluster_end);
            std::string theirs_full =
                reconstruct_side(Side::Theirs, all_hunks, members, cluster_start, cluster_end);

            if (ours_full == theirs_full) {
                content += ours_full;
            } else {
                result.status = MergeStatus::Conflicted;
                content += "<<<<<<< local\n";
                content += ensure_trailing_newline(ours_full);
                content += "=======\n";
                content += ensure_trailing_newline(theirs_full);
                content += ">>>>>>> remote\n";
            }
        }

        cursor = cluster_end;
        idx = next;
    }

    if (cursor < base.size()) {
        content += base_range(cursor, base.size());
    }

    return result;
}
