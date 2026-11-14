#pragma once

#include "hasher.hpp"
#include "manifest.hpp"
#include "remote_backend.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <system_error>

// A RemoteBackend double that keeps the "remote" side entirely in memory,
// so SyncService can be exercised end to end in a unit test without a
// second real directory tree or an HTTP server. upload()/download()/
// save_conflict_copy() still touch the real filesystem on the local side,
// since that's the path SyncService hands them.
class InMemoryRemoteBackend : public RemoteBackend {
  public:
    Manifest load_manifest() override {
        Manifest manifest;

        for (const auto &[path, entry] : files_) {
            manifest[path] =
                FileMeta{path, sha256_bytes(entry.content), entry.content.size(), entry.version};
        }

        return manifest;
    }

    void upload(const std::string &path, const std::filesystem::path &local_file) override {
        files_[path] = {read_file(local_file), next_version_++};
    }

    void download(const std::string &path, const std::filesystem::path &local_file,
                  const std::string &expected_hash = "") override {
        const auto &content = files_.at(path).content;

        if (!expected_hash.empty() && sha256_bytes(content) != expected_hash) {
            throw std::runtime_error("Downloaded content hash mismatch for " + path);
        }

        write_file(local_file, content);
    }

    void delete_remote(const std::string &path) override { files_.erase(path); }

    void save_conflict_copy(const std::string &path, const std::filesystem::path &local_conflict_path,
                            const std::string &expected_hash = "") override {
        auto it = files_.find(path);

        if (it == files_.end()) {
            return;
        }

        if (!expected_hash.empty() && sha256_bytes(it->second.content) != expected_hash) {
            throw std::runtime_error("Downloaded content hash mismatch for " + path);
        }

        if (std::filesystem::exists(local_conflict_path)) {
            throw std::filesystem::filesystem_error("Refusing to overwrite existing file",
                                                    local_conflict_path,
                                                    std::make_error_code(std::errc::file_exists));
        }

        write_file(local_conflict_path, it->second.content);
    }

    // Test-only convenience: sets the "remote" content for a path directly,
    // e.g. to simulate another device uploading mid-sync.
    void set_content(const std::string &path, const std::string &content) {
        files_[path] = {content, next_version_++};
    }

  private:
    struct Entry {
        std::string content;
        std::int64_t version;
    };

    static std::string read_file(const std::filesystem::path &path) {
        std::ifstream in(path, std::ios::binary);

        if (!in) {
            throw std::runtime_error("Could not open file: " + path.string());
        }

        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    static void write_file(const std::filesystem::path &path, const std::string &content) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    std::map<std::string, Entry> files_;
    std::int64_t next_version_ = 1;
};
