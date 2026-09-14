#include "http_server.hpp"

#include "scanner.hpp"
#include "security.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

class RateLimiter {
  public:
    explicit RateLimiter(int max_requests_per_minute)
        : max_requests_per_minute_(max_requests_per_minute) {}

    bool allow(const std::string &client_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = Clock::now();
        auto window_start = now - std::chrono::minutes(1);

        auto &requests = requests_by_client_[client_key];

        while (!requests.empty() && requests.front() < window_start) {
            requests.pop_front();
        }

        if (static_cast<int>(requests.size()) >= max_requests_per_minute_) {
            return false;
        }

        requests.push_back(now);
        return true;
    }

  private:
    int max_requests_per_minute_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::deque<Clock::time_point>> requests_by_client_;
};

static bool is_safe_relative_path(const std::string &path) {
    if (path.empty()) {
        return false;
    }

    fs::path parsed(path);

    if (parsed.is_absolute()) {
        return false;
    }

    for (const auto &part : parsed) {
        if (part == "..") {
            return false;
        }
    }

    return true;
}

static json manifest_to_json(const std::vector<FileMeta> &files) {
    json root;
    root["files"] = json::array();

    for (const auto &file : files) {
        root["files"].push_back({{"path", file.path},
                                 {"hash", file.hash},
                                 {"size", file.size},
                                 {"modified_time", file.modified_time}});
    }

    return root;
}

static bool read_file(const fs::path &path, std::string &body) {
    std::ifstream in(path, std::ios::binary);

    if (!in) {
        return false;
    }

    std::ostringstream stream;
    stream << in.rdbuf();
    body = stream.str();
    return true;
}

static bool write_file(const fs::path &path, const std::string &body) {
    fs::create_directories(path.parent_path());

    std::ofstream out(path, std::ios::binary);

    if (!out) {
        return false;
    }

    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    return out.good();
}

static bool request_path(const httplib::Request &req, std::string &path) {
    if (!req.has_param("path")) {
        return false;
    }

    path = req.get_param_value("path");
    return is_safe_relative_path(path);
}

static bool is_authorized(const httplib::Request &req, const std::string &bearer_token) {
    if (!req.has_header("Authorization")) {
        return false;
    }

    return constant_time_equals(req.get_header_value("Authorization"),
                                bearer_authorization_header(bearer_token));
}

void run_http_server(const fs::path &server_root,
                     const std::string &host,
                     int port,
                     const std::string &bearer_token,
                     std::size_t max_upload_bytes,
                     int rate_limit_per_minute) {
    httplib::Server server;
    RateLimiter rate_limiter(rate_limit_per_minute);

    server.set_payload_max_length(max_upload_bytes);
    server.set_pre_routing_handler(
        [&bearer_token, &rate_limiter](const httplib::Request &req, httplib::Response &res) {
            if (!rate_limiter.allow(req.remote_addr)) {
                res.status = 429;
                res.set_content("Too many requests\n", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }

            if (!is_authorized(req, bearer_token)) {
                res.status = 401;
                res.set_content("Unauthorized\n", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }

            return httplib::Server::HandlerResponse::Unhandled;
        });

    server.Get("/manifest", [server_root](const httplib::Request &, httplib::Response &res) {
        auto files = scan_vault(server_root);
        res.set_content(manifest_to_json(files).dump(2), "application/json");
    });

    server.Get("/file", [server_root](const httplib::Request &req, httplib::Response &res) {
        std::string relative_path;

        if (!request_path(req, relative_path)) {
            res.status = 400;
            res.set_content("Invalid path\n", "text/plain");
            return;
        }

        fs::path full_path = server_root / fs::path(relative_path);

        if (!fs::exists(full_path) || !fs::is_regular_file(full_path)) {
            res.status = 404;
            res.set_content("Not found\n", "text/plain");
            return;
        }

        std::string body;

        if (!read_file(full_path, body)) {
            res.status = 500;
            res.set_content("Failed to read file\n", "text/plain");
            return;
        }

        res.set_content(body, "application/octet-stream");
    });

    server.Put("/file", [server_root](const httplib::Request &req, httplib::Response &res) {
        std::string relative_path;

        if (!request_path(req, relative_path)) {
            res.status = 400;
            res.set_content("Invalid path\n", "text/plain");
            return;
        }

        fs::path full_path = server_root / fs::path(relative_path);

        if (!write_file(full_path, req.body)) {
            res.status = 500;
            res.set_content("Failed to write file\n", "text/plain");
            return;
        }

        res.status = 204;
    });

    server.Delete("/file", [server_root](const httplib::Request &req, httplib::Response &res) {
        std::string relative_path;

        if (!request_path(req, relative_path)) {
            res.status = 400;
            res.set_content("Invalid path\n", "text/plain");
            return;
        }

        fs::path full_path = server_root / fs::path(relative_path);

        if (fs::exists(full_path) && !fs::is_regular_file(full_path)) {
            res.status = 400;
            res.set_content("Path is not a regular file\n", "text/plain");
            return;
        }

        fs::remove(full_path);
        res.status = 204;
    });

    std::cout << "Serving " << server_root << " on http://" << host << ":" << port << "\n";
    std::cout << "Max upload size: " << max_upload_bytes << " bytes\n";
    std::cout << "Rate limit: " << rate_limit_per_minute << " requests/minute\n";

    if (!server.listen(host, port)) {
        throw std::runtime_error("Failed to start HTTP server");
    }
}
