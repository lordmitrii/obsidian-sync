#pragma once

#include "remote_backend.hpp"
#include "version_history.hpp"

#include <cstddef>
#include <string>
#include <vector>

class HttpRemoteBackend : public RemoteBackend {
  public:
    HttpRemoteBackend(std::string remote_url, std::string bearer_token,
                      std::size_t max_upload_bytes, std::string cacert_path = "");

    Manifest load_manifest() override;
    void upload(const std::string &path, const std::filesystem::path &local_file) override;
    void download(const std::string &path, const std::filesystem::path &local_file) override;
    void delete_remote(const std::string &path) override;
    void save_conflict_copy(const std::string &path,
                            const std::filesystem::path &local_conflict_path) override;

    std::vector<FileVersion> history(const std::string &path) const;
    void restore_version(const std::string &path, const std::string &hash) const;
    void prune() const;

  private:
    std::string remote_url_;
    std::string bearer_token_;
    std::size_t max_upload_bytes_;
    std::string cacert_path_;
};
