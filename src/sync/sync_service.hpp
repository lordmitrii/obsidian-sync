#pragma once

#include "remote_backend.hpp"

#include <filesystem>
#include <string>

class SyncService {
  public:
    SyncService(std::filesystem::path local_root,
                std::string state_db_path,
                RemoteBackend &remote_backend,
                bool apply);

    // Returns false if any action failed to apply (they are logged and left
    // for the next run); the sync still proceeds as far as it can.
    bool run_once();

  private:
    std::filesystem::path local_root_;
    std::string state_db_path_;
    RemoteBackend &remote_backend_;
    bool apply_;
};
