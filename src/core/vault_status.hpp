#pragma once

#include "manifest.hpp"

#include <cstdint>
#include <filesystem>

struct VaultStatus {
    std::size_t tracked_files = 0;
    std::uintmax_t tracked_bytes = 0;
    std::size_t unresolved_conflicts = 0;
    std::size_t trashed_files = 0;
};

VaultStatus compute_vault_status(const Manifest &base_manifest,
                                 const std::filesystem::path &vault_path);
