#include "vault_status.hpp"

#include "atomic_file.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

static bool is_all_digits(const std::string &text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(),
                      [](unsigned char c) { return std::isdigit(c) != 0; });
}

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

// Strips the ".conflict-remote" marker (and its numbering, if any) from a
// conflict copy's filename, leaving the original note's name. The numbering
// lands on different sides of the marker depending on whether the original
// note had an extension: "note.conflict-remote.2.md" -> "note.md", but
// "LICENSE.2.conflict-remote" -> "LICENSE" (no extension to keep it before).
static fs::path original_note_relative_path(const fs::path &conflict_relative_path) {
    const std::string filename = conflict_relative_path.filename().string();
    const std::string marker = ".conflict-remote";
    const auto marker_pos = filename.find(marker);

    if (marker_pos == std::string::npos) {
        return conflict_relative_path;
    }

    std::string base = filename.substr(0, marker_pos);
    std::string suffix = filename.substr(marker_pos + marker.size());

    if (!suffix.empty() && suffix.front() == '.') {
        const auto next_dot = suffix.find('.', 1);
        const std::string number = suffix.substr(1, next_dot == std::string::npos
                                                       ? std::string::npos
                                                       : next_dot - 1);

        if (is_all_digits(number)) {
            suffix = next_dot == std::string::npos ? "" : suffix.substr(next_dot);
        }
    } else if (suffix.empty()) {
        const auto last_dot = base.rfind('.');

        if (last_dot != std::string::npos && is_all_digits(base.substr(last_dot + 1))) {
            base = base.substr(0, last_dot);
        }
    }

    return conflict_relative_path.parent_path() / (base + suffix);
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

std::vector<std::string> list_conflicted_notes(const fs::path &vault_path) {
    std::set<std::string> notes;

    if (!fs::exists(vault_path)) {
        return {};
    }

    for (const auto &entry : fs::recursive_directory_iterator(vault_path)) {
        if (entry.is_symlink() || !entry.is_regular_file() || is_in_trash(entry.path())) {
            continue;
        }

        if (!is_conflict_copy(entry.path())) {
            continue;
        }

        fs::path relative_path = fs::relative(entry.path(), vault_path);
        notes.insert(original_note_relative_path(relative_path).generic_string());
    }

    return {notes.begin(), notes.end()};
}

static fs::path conflict_copy_candidate(const fs::path &note_absolute_path, int n) {
    fs::path first_choice = note_absolute_path.parent_path() /
                            (note_absolute_path.stem().string() + ".conflict-remote" +
                             note_absolute_path.extension().string());

    if (n == 1) {
        return first_choice;
    }

    return first_choice.parent_path() /
           (first_choice.stem().string() + "." + std::to_string(n) +
            first_choice.extension().string());
}

static std::vector<fs::path> existing_conflict_copies(const fs::path &note_absolute_path) {
    std::vector<fs::path> copies;

    for (int n = 1;; ++n) {
        fs::path candidate = conflict_copy_candidate(note_absolute_path, n);

        if (!fs::exists(candidate)) {
            break;
        }

        copies.push_back(candidate);
    }

    return copies;
}

ConflictResolution parse_conflict_resolution(const std::string &text) {
    if (text == "local") {
        return ConflictResolution::KeepLocal;
    }

    if (text == "remote") {
        return ConflictResolution::KeepRemote;
    }

    throw std::runtime_error("--keep must be local or remote");
}

bool resolve_conflict(const fs::path &vault_path, const std::string &note_relative_path,
                      ConflictResolution resolution) {
    fs::path note_absolute_path = vault_path / note_relative_path;
    std::vector<fs::path> copies = existing_conflict_copies(note_absolute_path);

    if (copies.empty()) {
        return false;
    }

    if (resolution == ConflictResolution::KeepRemote) {
        atomic_copy_file(copies.back(), note_absolute_path);
    }

    fs::path trash_root = vault_path / ".trash";

    for (const auto &copy : copies) {
        move_to_trash(copy, trash_root, fs::relative(copy, vault_path));
    }

    return true;
}
