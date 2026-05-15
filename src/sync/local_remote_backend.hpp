#pragma once

#include "remote_backend.hpp"

#include <filesystem>

class LocalRemoteBackend : public RemoteBackend {
  public:
    explicit LocalRemoteBackend(std::filesystem::path remote_root);

    Manifest load_manifest() override;
    void upload(const std::string &path, const std::filesystem::path &local_file) override;
    void download(const std::string &path, const std::filesystem::path &local_file) override;
    void delete_remote(const std::string &path) override;
    void save_conflict_copy(const std::string &path,
                            const std::filesystem::path &local_conflict_path) override;

  private:
    std::filesystem::path remote_root_;
};
