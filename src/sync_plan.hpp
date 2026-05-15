#pragma once

#include "db.hpp"
#include "filemeta.hpp"

#include <string>
#include <vector>

enum class SyncActionType {
    Created,
    Modified,
    Unchanged,
    Deleted
};

struct SyncAction {
    SyncActionType type;
    std::string path;
};

std::vector<SyncAction> build_sync_plan(
    Database& db,
    const std::vector<FileMeta>& current_files
);

std::string action_type_to_string(SyncActionType type);