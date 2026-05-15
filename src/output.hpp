#pragma once

#include <vector>

#include "filemeta.hpp"
#include "sync_plan.hpp"

void print_json_manifest(const std::vector<FileMeta> &files);
void print_text_actions(const std::vector<SyncAction> &actions);
void print_json_actions(const std::vector<SyncAction> &actions);
void write_json_manifest_to_file(const std::vector<FileMeta> &files,
                                 const std::string &output_path);