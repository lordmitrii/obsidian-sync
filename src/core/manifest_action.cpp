#include "manifest_action.hpp"

std::string manifest_action_to_string(ManifestActionType type) {
    switch (type) {
        case ManifestActionType::Upload:
            return "upload";
        case ManifestActionType::Download:
            return "download";
        case ManifestActionType::DeleteLocal:
            return "delete-local";
        case ManifestActionType::DeleteRemote:
            return "delete-remote";
        case ManifestActionType::Conflict:
            return "conflict";
        case ManifestActionType::Unchanged:
            return "unchanged";
    }

    return "unknown";
}
