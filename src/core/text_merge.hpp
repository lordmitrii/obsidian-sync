#pragma once

#include <string>

enum class OnConflictPolicy { Copy, Merge };

OnConflictPolicy parse_on_conflict_policy(const std::string &text);

enum class MergeStatus { Clean, Conflicted };

struct MergeResult {
    MergeStatus status = MergeStatus::Clean;
    std::string content;
};

MergeResult merge_three_way_text(const std::string &base,
                                 const std::string &ours,
                                 const std::string &theirs);
