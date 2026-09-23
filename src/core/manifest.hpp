#pragma once

#include "filemeta.hpp"

#include <string>
#include <unordered_map>
#include <vector>

using Manifest = std::unordered_map<std::string, FileMeta>;

Manifest load_manifest(const std::string& path);
Manifest parse_manifest_json(const std::string& json_text);