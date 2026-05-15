#pragma once

#include "filemeta.hpp"

#include <filesystem>
#include <vector>

bool should_ignore(const std::filesystem::path& path);

std::vector<FileMeta> scan_vault(const std::filesystem::path& vault_path);