#pragma once

#include "remote_backend.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

enum class RunStatus { Success, ActionsFailed, ConflictsPending, DeletesBlocked };

struct DeleteSafetyLimits {
    bool force = false;
    size_t max_count = 50;
    double max_percent = 50.0;
};

class SyncService {
  public:
    SyncService(std::filesystem::path local_root,
                std::string state_db_path,
                RemoteBackend &remote_backend,
                bool apply,
                DeleteSafetyLimits delete_limits = {},
                int trash_retention_days = 30);

    RunStatus run_once();

  private:
    std::filesystem::path local_root_;
    std::string state_db_path_;
    RemoteBackend &remote_backend_;
    bool apply_;
    DeleteSafetyLimits delete_limits_;
    int trash_retention_days_;
};
