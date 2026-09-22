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
