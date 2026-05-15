#pragma once

#include "filemeta.hpp"

#include <filesystem>
#include <optional>
#include <vector>

bool should_ignore(const std::filesystem::path &path);

std::optional<FileMeta> scan_file(const std::filesystem::path &root,
                                  const std::filesystem::path &relative_path);

std::vector<FileMeta> scan_vault(const std::filesystem::path &vault_path);
