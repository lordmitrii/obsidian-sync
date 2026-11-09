#pragma once

#include <string>

enum class MergeStatus { Clean, Conflicted };

struct MergeResult {
    MergeStatus status = MergeStatus::Clean;
    std::string content;
};

// Line-based three-way (diff3-style) merge of a text file. `base` is the
// common ancestor; `ours`/`theirs` are the two sides that each may have
// edited it since. Edits to disjoint regions merge cleanly, edits to the
// same region merge cleanly only if both sides produced identical text,
// and any other overlap is reported as a conflict with git-style markers
// (<<<<<<< local / ======= / >>>>>>> remote) around the full region.
MergeResult merge_three_way_text(const std::string &base,
                                 const std::string &ours,
                                 const std::string &theirs);
