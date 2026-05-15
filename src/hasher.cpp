#include "hasher.hpp"

#include <openssl/evp.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();

    if (!ctx) {
        throw std::runtime_error("Failed to create EVP context");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    char buffer[8192];

    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        std::streamsize bytes_read = file.gcount();

        if (bytes_read > 0) {
            if (EVP_DigestUpdate(ctx, buffer, bytes_read) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestUpdate failed");
            }
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_length = 0;

    if (EVP_DigestFinal_ex(ctx, hash, &hash_length) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    EVP_MD_CTX_free(ctx);

    std::ostringstream result;

    for (unsigned int i = 0; i < hash_length; ++i) {
        result << std::hex
               << std::setw(2)
               << std::setfill('0')
               << static_cast<int>(hash[i]);
    }

    return result.str();
}