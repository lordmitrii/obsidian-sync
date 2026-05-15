#pragma once

#include "manifest.hpp"
#include "manifest_action.hpp"

#include <filesystem>
#include <string>
#include <vector>

Manifest fetch_remote_manifest(const std::string &remote_url);

void execute_http_actions(const std::vector<ManifestAction> &actions,
                          const std::filesystem::path &local_root,
                          const std::string &remote_url);
