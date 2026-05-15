#pragma once

#include "manifest.hpp"

#include <filesystem>
#include <string>

class RemoteBackend {
  public:
    virtual ~RemoteBackend() = default;

    virtual Manifest load_manifest() = 0;
    virtual void upload(const std::string &path, const std::filesystem::path &local_file) = 0;
    virtual void download(const std::string &path, const std::filesystem::path &local_file) = 0;
    virtual void delete_remote(const std::string &path) = 0;
    virtual void save_conflict_copy(const std::string &path,
                                    const std::filesystem::path &local_conflict_path) = 0;
};
