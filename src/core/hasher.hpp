#pragma once

#include <cstddef>
#include <filesystem>
#include <openssl/evp.h>
#include <string>

std::string sha256_file(const std::filesystem::path &path);
std::string sha256_bytes(const std::string &data);

class IncrementalSha256 {
  public:
    IncrementalSha256();
    ~IncrementalSha256();

    IncrementalSha256(const IncrementalSha256 &) = delete;
    IncrementalSha256 &operator=(const IncrementalSha256 &) = delete;

    void update(const char *data, std::size_t length);
    std::string digest();

  private:
    EVP_MD_CTX *ctx_;
};