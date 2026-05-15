#pragma once

#include <string>

enum class ManifestActionType {
    Upload,
    Download,
    DeleteLocal,
    DeleteRemote,
    Conflict,
    Unchanged
};

struct ManifestAction {
    ManifestActionType type;
    std::string path;
};

std::string manifest_action_to_string(ManifestActionType type);
