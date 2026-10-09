#include "vault_status.hpp"

namespace fs = std::filesystem;

static bool is_conflict_copy(const fs::path &path) {
    const std::string filename = path.filename().string();
    return filename.find(".conflict-remote.") != std::string::npos ||
           filename.ends_with(".conflict-remote");
}

static bool is_in_trash(const fs::path &path) {
    for (const auto &part : path) {
        if (part == ".trash")
            return true;
    }

    return false;
}

VaultStatus compute_vault_status(const Manifest &base_manifest, const fs::path &vault_path) {
    VaultStatus status;
    status.tracked_files = base_manifest.size();

    for (const auto &[path, meta] : base_manifest) {
        status.tracked_bytes += meta.size;
    }

    if (!fs::exists(vault_path)) {
        return status;
    }

    for (const auto &entry : fs::recursive_directory_iterator(vault_path)) {
        if (entry.is_symlink() || !entry.is_regular_file()) {
            continue;
        }

        if (is_in_trash(entry.path())) {
            ++status.trashed_files;
        } else if (is_conflict_copy(entry.path())) {
            ++status.unresolved_conflicts;
        }
    }

    return status;
}
