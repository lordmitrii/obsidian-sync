#include "http_remote_backend.hpp"

#include "security.hpp"

#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct HttpResponse {
    long status = 0;
    std::string body;
};

static std::string trim_trailing_slashes(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    return url;
}

static std::size_t write_to_string(char *ptr, std::size_t size, std::size_t nmemb, void *userdata) {
    auto *body = static_cast<std::string *>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string escape_query_value(CURL *curl, const std::string &value) {
    char *escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));

    if (escaped == nullptr) {
        throw std::runtime_error("Failed to escape URL query value");
    }

    std::string result(escaped);
    curl_free(escaped);
    return result;
}

static std::string file_url(const std::string &remote_url, const std::string &path) {
    CURL *curl = curl_easy_init();

    if (curl == nullptr) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string escaped_path = escape_query_value(curl, path);
    curl_easy_cleanup(curl);

    return trim_trailing_slashes(remote_url) + "/file?path=" + escaped_path;
}

static std::string manifest_url(const std::string &remote_url) {
    return trim_trailing_slashes(remote_url) + "/manifest";
}

static HttpResponse request(const std::string &method,
                            const std::string &url,
                            const std::string &bearer_token,
                            const std::string *body = nullptr) {
    CURL *curl = curl_easy_init();

    if (curl == nullptr) {
        throw std::runtime_error("Failed to initialize curl");
    }

    HttpResponse response;
    curl_slist *headers = nullptr;
    std::string auth_header = "Authorization: " + bearer_authorization_header(bearer_token);
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, DEFAULT_HTTP_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, DEFAULT_HTTP_REQUEST_TIMEOUT_SECONDS);

    if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body != nullptr ? body->data() : "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         body != nullptr ? static_cast<long>(body->size()) : 0L);
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode code = curl_easy_perform(curl);

    if (code != CURLE_OK) {
        std::string message = curl_easy_strerror(code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error("HTTP request failed: " + message);
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

static Manifest parse_manifest(const std::string &body) {
    json root = json::parse(body);
    Manifest manifest;

    for (const auto &item : root["files"]) {
        FileMeta meta;
        meta.path = item["path"];
        meta.hash = item["hash"];
        meta.size = item["size"];
        meta.modified_time = item["modified_time"];

        manifest[meta.path] = meta;
    }

    return manifest;
}

static std::string read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);

    if (!in) {
        throw std::runtime_error("Failed to open local file for reading: " + path.string());
    }

    std::ostringstream stream;
    stream << in.rdbuf();
    return stream.str();
}

static void write_file(const fs::path &path, const std::string &body, bool overwrite) {
    fs::create_directories(path.parent_path());

    if (!overwrite && fs::exists(path)) {
        throw std::runtime_error("Refusing to overwrite file: " + path.string());
    }

    std::ofstream out(path, std::ios::binary);

    if (!out) {
        throw std::runtime_error("Failed to open local file for writing: " + path.string());
    }

    out.write(body.data(), static_cast<std::streamsize>(body.size()));

    if (!out.good()) {
        throw std::runtime_error("Failed to write local file: " + path.string());
    }
}

HttpRemoteBackend::HttpRemoteBackend(std::string remote_url,
                                     std::string bearer_token,
                                     std::size_t max_upload_bytes)
    : remote_url_(std::move(remote_url)),
      bearer_token_(std::move(bearer_token)),
      max_upload_bytes_(max_upload_bytes) {}

Manifest HttpRemoteBackend::load_manifest() {
    HttpResponse response = request("GET", manifest_url(remote_url_), bearer_token_);

    if (response.status != 200) {
        throw std::runtime_error("Failed to fetch remote manifest: HTTP " +
                                 std::to_string(response.status));
    }

    return parse_manifest(response.body);
}

void HttpRemoteBackend::upload(const std::string &path, const fs::path &local_file) {
    if (fs::file_size(local_file) > max_upload_bytes_) {
        throw std::runtime_error("Upload exceeds maximum size: " + path);
    }

    std::string body = read_file(local_file);
    HttpResponse response = request("PUT", file_url(remote_url_, path), bearer_token_, &body);

    if (response.status == 413) {
        throw std::runtime_error("Upload rejected as too large: " + path);
    }

    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("Upload failed for " + path + ": HTTP " +
                                 std::to_string(response.status));
    }
}

void HttpRemoteBackend::download(const std::string &path, const fs::path &local_file) {
    HttpResponse response = request("GET", file_url(remote_url_, path), bearer_token_);

    if (response.status != 200) {
        throw std::runtime_error("Download failed for " + path + ": HTTP " +
                                 std::to_string(response.status));
    }

    write_file(local_file, response.body, true);
}

void HttpRemoteBackend::delete_remote(const std::string &path) {
    HttpResponse response = request("DELETE", file_url(remote_url_, path), bearer_token_);

    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("Remote delete failed for " + path + ": HTTP " +
                                 std::to_string(response.status));
    }
}

void HttpRemoteBackend::save_conflict_copy(const std::string &path,
                                           const fs::path &local_conflict_path) {
    HttpResponse response = request("GET", file_url(remote_url_, path), bearer_token_);

    if (response.status == 404) {
        return;
    }

    if (response.status != 200) {
        throw std::runtime_error("Conflict download failed for " + path + ": HTTP " +
                                 std::to_string(response.status));
    }

    write_file(local_conflict_path, response.body, false);
}
