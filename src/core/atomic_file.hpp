#pragma once

#include <filesystem>
#include <string>

// Writes `body` to `path` without ever leaving a partially-written file in
// place: the data is written to a temporary file in the same directory and
// then renamed over the destination. Throws std::runtime_error on failure;
// `path` is left untouched if the write fails.
void atomic_write_file(const std::filesystem::path &path, const std::string &body);

// Copies `source` to `destination` the same way: via a temporary file in
// destination's directory, renamed into place once the copy succeeds.
void atomic_copy_file(const std::filesystem::path &source, const std::filesystem::path &destination);

std::filesystem::path unique_path(const std::filesystem::path &first_choice);

// Moves `source` into `trash_root` at `relative_path`, numbering on
// collision (see unique_path) instead of clobbering an earlier trashed file
// at the same path. Falls back to copy+remove when rename can't cross
// filesystems. Returns the path it was moved to.
std::filesystem::path move_to_trash(const std::filesystem::path &source,
                                    const std::filesystem::path &trash_root,
                                    const std::filesystem::path &relative_path);

// Permanently removes files under trash_root that were moved there more
// than retention_days ago, then prunes any directories left empty. A
// retention_days of 0 or less disables cleanup.
void cleanup_trash(const std::filesystem::path &trash_root, int retention_days);
