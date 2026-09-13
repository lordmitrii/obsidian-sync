#pragma once

#include <cstddef>
#include <string>

constexpr std::size_t DEFAULT_MAX_UPLOAD_BYTES = 25 * 1024 * 1024;
constexpr int DEFAULT_RATE_LIMIT_PER_MINUTE = 120;
constexpr long DEFAULT_HTTP_CONNECT_TIMEOUT_SECONDS = 10;
constexpr long DEFAULT_HTTP_REQUEST_TIMEOUT_SECONDS = 60;

std::string load_required_bearer_token();
std::string bearer_authorization_header(const std::string &token);
bool constant_time_equals(const std::string &a, const std::string &b);
