#pragma once

#include <filesystem>
#include <string>

std::string sha256_file(const std::filesystem::path &path);
std::string sha256_bytes(const std::string &data);