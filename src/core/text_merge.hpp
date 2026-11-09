#pragma once

#include <string>

// Copy (the default): keep today's behaviour, every conflict gets a
// name.conflict-remote[.N] copy next to the note. Merge: attempt the
// three-way merge below first and only fall back to a conflict copy when
// the merge itself finds an overlapping, unresolvable edit.
enum class OnConflictPolicy { Copy, Merge };

OnConflictPolicy parse_on_conflict_policy(const std::string &text);

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
