#pragma once

#include <chrono>
#include <string>

std::string format_timestamp_iso8601(std::chrono::system_clock::time_point time);
std::string current_timestamp_iso8601();
