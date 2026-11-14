#include "http_remote_backend.hpp"

#include "atomic_file.hpp"
#include "manifest.hpp"
#include "security.hpp"

#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

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

struct FileWriteContext {
    AtomicFileWriter *writer;
    std::string error;
};

static std::size_t write_to_file(char *ptr, std::size_t size, std::size_t nmemb, void *userdata) {
    auto *ctx = static_cast<FileWriteContext *>(userdata);
    std::size_t length = size * nmemb;

    try {
        ctx->writer->write(ptr, length);
        return length;
    } catch (const std::exception &e) {
        ctx->error = e.what();
        return 0;
    }
}

static std::size_t read_from_file(char *buf, std::size_t size, std::size_t nmemb, void *userdata) {
    auto *stream = static_cast<std::ifstream *>(userdata);
    stream->read(buf, static_cast<std::streamsize>(size * nmemb));
    return static_cast<std::size_t>(stream->gcount());
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

static std::string history_url(const std::string &remote_url, const std::string &path) {
    CURL *curl = curl_easy_init();

    if (curl == nullptr) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string escaped_path = escape_query_value(curl, path);
    curl_easy_cleanup(curl);

    return trim_trailing_slashes(remote_url) + "/history?path=" + escaped_path;
}

static std::string restore_url(const std::string &remote_url, const std::string &path,
                               const std::string &hash) {
    CURL *curl = curl_easy_init();

    if (curl == nullptr) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string escaped_path = escape_query_value(curl, path);
    std::string escaped_hash = escape_query_value(curl, hash);
    curl_easy_cleanup(curl);

    return trim_trailing_slashes(remote_url) + "/restore?path=" + escaped_path +
           "&hash=" + escaped_hash;
}

static std::string prune_url(const std::string &remote_url) {
    return trim_trailing_slashes(remote_url) + "/prune";
}

static CURL *init_curl_request(const std::string &url, const std::string &bearer_token,
                               const std::string &cacert_path, curl_slist *&headers) {
    CURL *curl = curl_easy_init();

    if (curl == nullptr) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string auth_header = "Authorization: " + bearer_authorization_header(bearer_token);
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, DEFAULT_HTTP_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, DEFAULT_HTTP_REQUEST_TIMEOUT_SECONDS);

    if (!cacert_path.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, cacert_path.c_str());
    }

    return curl;
}

static HttpResponse request(const std::string &method, const std::string &url,
                            const std::string &bearer_token, const std::string &cacert_path) {
    curl_slist *headers = nullptr;
    CURL *curl = init_curl_request(url, bearer_token, cacert_path, headers);

    HttpResponse response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
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

static long stream_download(const std::string &url, const std::string &bearer_token,
                            const std::string &cacert_path, AtomicFileWriter &writer) {
    curl_slist *headers = nullptr;
    CURL *curl = init_curl_request(url, bearer_token, cacert_path, headers);

    FileWriteContext ctx{&writer, ""};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode code = curl_easy_perform(curl);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (!ctx.error.empty()) {
        throw std::runtime_error(ctx.error);
    }

    if (code != CURLE_OK) {
        throw std::runtime_error("HTTP request failed: " + std::string(curl_easy_strerror(code)));
    }

    return status;
}

HttpRemoteBackend::HttpRemoteBackend(std::string remote_url, std::string bearer_token,
                                     std::size_t max_upload_bytes, std::string cacert_path)
    : remote_url_(std::move(remote_url)), bearer_token_(std::move(bearer_token)),
      max_upload_bytes_(max_upload_bytes), cacert_path_(std::move(cacert_path)) {}

Manifest HttpRemoteBackend::load_manifest() {
    HttpResponse response = request("GET", manifest_url(remote_url_), bearer_token_, cacert_path_);

    if (response.status != 200) {
        throw std::runtime_error("Failed to fetch remote manifest: HTTP " +
                                 std::to_string(response.status));
    }

    return parse_manifest_json(response.body);
}

void HttpRemoteBackend::upload(const std::string &path, const fs::path &local_file) {
    auto file_size = fs::file_size(local_file);

    if (file_size > max_upload_bytes_) {
        throw std::runtime_error("Upload exceeds maximum size: " + path);
    }

    std::ifstream in(local_file, std::ios::binary);

    if (!in) {
        throw std::runtime_error("Failed to open local file for reading: " + local_file.string());
    }

    curl_slist *headers = nullptr;
    CURL *curl =
        init_curl_request(file_url(remote_url_, path), bearer_token_, cacert_path_, headers);

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(file_size));
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_from_file);
    curl_easy_setopt(curl, CURLOPT_READDATA, &in);

    CURLcode code = curl_easy_perform(curl);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        throw std::runtime_error("HTTP request failed: " + std::string(curl_easy_strerror(code)));
    }

    if (status == 413) {
        throw std::runtime_error("Upload rejected as too large: " + path);
    }

    if (status < 200 || status >= 300) {
        throw std::runtime_error("Upload failed for " + path + ": HTTP " + std::to_string(status));
    }
}

void HttpRemoteBackend::download(const std::string &path, const fs::path &local_file) {
    AtomicFileWriter writer(local_file);
    long status = stream_download(file_url(remote_url_, path), bearer_token_, cacert_path_, writer);

    if (status != 200) {
        throw std::runtime_error("Download failed for " + path + ": HTTP " + std::to_string(status));
    }

    writer.commit();
}

void HttpRemoteBackend::delete_remote(const std::string &path) {
    HttpResponse response =
        request("DELETE", file_url(remote_url_, path), bearer_token_, cacert_path_);

    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("Remote delete failed for " + path + ": HTTP " +
                                 std::to_string(response.status));
    }
}

void HttpRemoteBackend::save_conflict_copy(const std::string &path,
                                           const fs::path &local_conflict_path) {
    AtomicFileWriter writer(local_conflict_path);
    long status =
        stream_download(file_url(remote_url_, path), bearer_token_, cacert_path_, writer);

    if (status == 404) {
        return;
    }

    if (status != 200) {
        throw std::runtime_error("Conflict download failed for " + path + ": HTTP " +
                                 std::to_string(status));
    }

    if (fs::exists(local_conflict_path)) {
        throw std::runtime_error("Refusing to overwrite file: " + local_conflict_path.string());
    }

    writer.commit();
}

std::vector<FileVersion> HttpRemoteBackend::history(const std::string &path) const {
    HttpResponse response =
        request("GET", history_url(remote_url_, path), bearer_token_, cacert_path_);

    if (response.status != 200) {
        throw std::runtime_error("Failed to fetch version history for " + path + ": HTTP " +
                                 std::to_string(response.status));
    }

    nlohmann::json j = nlohmann::json::parse(response.body);
    std::vector<FileVersion> versions;

    for (const auto &item : j) {
        FileVersion version;
        version.hash = item.at("hash").get<std::string>();
        version.timestamp = item.at("timestamp").get<std::string>();
        version.size = item.at("size").get<std::size_t>();
        versions.push_back(std::move(version));
    }

    return versions;
}

void HttpRemoteBackend::restore_version(const std::string &path, const std::string &hash) const {
    HttpResponse response =
        request("POST", restore_url(remote_url_, path, hash), bearer_token_, cacert_path_);

    if (response.status == 404) {
        throw std::runtime_error("No stored version " + hash + " for " + path);
    }

    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("Restore failed for " + path + ": HTTP " +
                                 std::to_string(response.status));
    }
}

void HttpRemoteBackend::prune() const {
    HttpResponse response = request("POST", prune_url(remote_url_), bearer_token_, cacert_path_);

    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("Prune failed: HTTP " + std::to_string(response.status));
    }
}
