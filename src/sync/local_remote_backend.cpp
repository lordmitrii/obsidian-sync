#include "local_remote_backend.hpp"

#include "scanner.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static void copy_file_overwriting(const fs::path &source, const fs::path &destination) {
    fs::create_directories(destination.parent_path());
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
}

static void copy_file_without_overwriting(const fs::path &source, const fs::path &destination) {
    fs::create_directories(destination.parent_path());
    fs::copy_file(source, destination, fs::copy_options::none);
}

LocalRemoteBackend::LocalRemoteBackend(fs::path remote_root)
    : remote_root_(std::move(remote_root)) {}

Manifest LocalRemoteBackend::load_manifest() {
    Manifest manifest;

    for (const auto &file : scan_vault(remote_root_)) {
        manifest[file.path] = file;
    }

    return manifest;
}

void LocalRemoteBackend::upload(const std::string &path, const fs::path &local_file) {
    std::cout << "Uploading " << path << "\n";
    copy_file_overwriting(local_file, remote_root_ / path);
}

void LocalRemoteBackend::download(const std::string &path, const fs::path &local_file) {
    std::cout << "Downloading " << path << "\n";
    copy_file_overwriting(remote_root_ / path, local_file);
}

void LocalRemoteBackend::delete_remote(const std::string &path) {
    std::cout << "Deleting remote " << path << "\n";
    fs::remove(remote_root_ / path);
}

void LocalRemoteBackend::save_conflict_copy(const std::string &path,
                                            const fs::path &local_conflict_path) {
    fs::path remote_path = remote_root_ / path;

    if (!fs::exists(remote_path)) {
        std::cout << "Remote version is missing; no conflict copy was saved\n";
        return;
    }

    copy_file_without_overwriting(remote_path, local_conflict_path);
    std::cout << "Saved remote version as " << local_conflict_path.string() << "\n";
}
