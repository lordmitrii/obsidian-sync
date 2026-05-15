#include "http_client.hpp"

#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

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

static HttpResponse request(const std::string &method,
                            const std::string &url,
                            const std::string *body = nullptr) {
    CURL *curl = curl_easy_init();

    if (curl == nullptr) {
        throw std::runtime_error("Failed to initialize curl");
    }

    HttpResponse response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

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
        curl_easy_cleanup(curl);
        throw std::runtime_error("HTTP request failed: " + message);
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    curl_easy_cleanup(curl);

    return response;
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

static fs::path remote_conflict_path(const fs::path &local_path) {
    return local_path.parent_path() /
           (local_path.stem().string() + ".conflict-remote" + local_path.extension().string());
}

Manifest fetch_remote_manifest(const std::string &remote_url) {
    HttpResponse response = request("GET", manifest_url(remote_url));

    if (response.status != 200) {
        throw std::runtime_error("Failed to fetch remote manifest: HTTP " +
                                 std::to_string(response.status));
    }

    return parse_manifest(response.body);
}

void execute_http_actions(const std::vector<ManifestAction> &actions,
                          const fs::path &local_root,
                          const std::string &remote_url) {
    for (const auto &action : actions) {
        fs::path local_path = local_root / action.path;

        switch (action.type) {
        case ManifestActionType::Upload: {
            std::string body = read_file(local_path);
            HttpResponse response = request("PUT", file_url(remote_url, action.path), &body);

            if (response.status < 200 || response.status >= 300) {
                throw std::runtime_error("Upload failed for " + action.path + ": HTTP " +
                                         std::to_string(response.status));
            }

            break;
        }

        case ManifestActionType::Download: {
            HttpResponse response = request("GET", file_url(remote_url, action.path));

            if (response.status != 200) {
                throw std::runtime_error("Download failed for " + action.path + ": HTTP " +
                                         std::to_string(response.status));
            }

            write_file(local_path, response.body, true);
            break;
        }

        case ManifestActionType::DeleteLocal:
            fs::remove(local_path);
            break;

        case ManifestActionType::DeleteRemote: {
            HttpResponse response = request("DELETE", file_url(remote_url, action.path));

            if (response.status < 200 || response.status >= 300) {
                throw std::runtime_error("Remote delete failed for " + action.path + ": HTTP " +
                                         std::to_string(response.status));
            }

            break;
        }

        case ManifestActionType::Conflict: {
            HttpResponse response = request("GET", file_url(remote_url, action.path));

            if (response.status == 404) {
                break;
            }

            if (response.status != 200) {
                throw std::runtime_error("Conflict download failed for " + action.path + ": HTTP " +
                                         std::to_string(response.status));
            }

            write_file(remote_conflict_path(local_path), response.body, false);
            break;
        }

        case ManifestActionType::Unchanged:
            break;
        }
    }
}
