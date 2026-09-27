#pragma once

#include "remote_backend.hpp"

#include <filesystem>
#include <string>

// ActionsFailed takes priority over ConflictsPending when a run has both:
// a failed action needs attention every run, while an unresolved conflict is
// expected to sit quietly until the user hand-merges it.
enum class RunStatus { Success, ActionsFailed, ConflictsPending };

class SyncService {
  public:
    SyncService(std::filesystem::path local_root,
                std::string state_db_path,
                RemoteBackend &remote_backend,
                bool apply);

    // Logs and skips any action that fails to apply (left for the next run)
    // rather than aborting the whole run. Unresolved conflicts (a
    // *.conflict-remote copy still sitting next to its file) are reported
    // separately from action failures via the returned status.
    RunStatus run_once();

  private:
    std::filesystem::path local_root_;
    std::string state_db_path_;
    RemoteBackend &remote_backend_;
    bool apply_;
};
