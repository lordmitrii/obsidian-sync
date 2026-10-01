#pragma once

#include "filemeta.hpp"
#include "ignore_rules.hpp"
#include "manifest.hpp"

#include <filesystem>
#include <optional>
#include <vector>

bool should_ignore(const std::filesystem::path &path);
bool should_ignore(const std::filesystem::path &path, const std::filesystem::path &relative_path,
                   const IgnoreRules &extra_rules);

// known_hashes lets the caller pass in a previously scanned manifest (e.g.
// the state database's base manifest): when a path's size and mtime still
// match, the stored hash is reused instead of re-reading and hashing the
// file. Left empty, every file is hashed unconditionally.
std::optional<FileMeta> scan_file(const std::filesystem::path &root,
                                  const std::filesystem::path &relative_path,
                                  const Manifest &known_hashes = {});

std::vector<FileMeta> scan_vault(const std::filesystem::path &vault_path,
                                 const Manifest &known_hashes = {},
                                 SyncConfigPreset sync_config = SyncConfigPreset::All);
