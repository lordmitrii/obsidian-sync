#pragma once

#include "manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct VaultStatus {
    std::size_t tracked_files = 0;
    std::uintmax_t tracked_bytes = 0;
    std::size_t unresolved_conflicts = 0;
    std::size_t trashed_files = 0;
};

VaultStatus compute_vault_status(const Manifest &base_manifest,
                                 const std::filesystem::path &vault_path);

// Relative paths (vault-root-relative, forward-slash separated) of notes that
// have at least one unresolved "name.conflict-remote[.N].ext" copy sitting
// next to them, sorted and deduplicated so a note with several numbered
// copies is still listed once.
std::vector<std::string> list_conflicted_notes(const std::filesystem::path &vault_path);

// The path of a note's nth conflict copy: n == 1 is
// "name.conflict-remote.ext", n >= 2 is "name.conflict-remote.n.ext".
std::filesystem::path conflict_copy_path(const std::filesystem::path &note_path, int n);

enum class ConflictResolution { KeepLocal, KeepRemote };

ConflictResolution parse_conflict_resolution(const std::string &text);
std::string conflict_resolution_to_string(ConflictResolution resolution);

// Resolves a note's conflict by deleting all of its "name.conflict-remote[.N]"
// copies (KeepLocal), or by replacing the note with the newest copy's
// contents before deleting the copies (KeepRemote). Deleted copies go to
// <vault_path>/.trash like any other removal. Returns false if the note has
// no unresolved conflict copies to resolve.
bool resolve_conflict(const std::filesystem::path &vault_path,
                      const std::string &note_relative_path, ConflictResolution resolution);
