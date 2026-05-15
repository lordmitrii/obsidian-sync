#pragma once

#include "manifest_compare.hpp"

#include <filesystem>
#include <vector>

void execute_manifest_actions(const std::vector<ManifestAction> &actions,
                              const std::filesystem::path &local_root,
                              const std::filesystem::path &remote_root);