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
