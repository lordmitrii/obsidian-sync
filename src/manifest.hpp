#pragma once

#include "filemeta.hpp"

#include <string>
#include <unordered_map>
#include <vector>

using Manifest = std::unordered_map<std::string, FileMeta>;

Manifest load_manifest(const std::string& path);