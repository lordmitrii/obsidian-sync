#pragma once

#include "sync_plan.hpp"

#include <vector>

void print_text_actions(const std::vector<SyncAction>& actions);
void print_json_actions(const std::vector<SyncAction>& actions);