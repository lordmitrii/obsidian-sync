#pragma once

#include "manifest.hpp"

#include <string>
#include <vector>

enum class ManifestActionType {
    Upload,
    Download,
    Conflict
};

struct ManifestAction {
    ManifestActionType type;
    std::string path;
};

std::vector<ManifestAction> compare_manifests(
    const Manifest& local,
    const Manifest& remote
);

std::string manifest_action_to_string(ManifestActionType type);