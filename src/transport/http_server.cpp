#include "http_server.hpp"

#include "atomic_file.hpp"
#include "output.hpp"
#include "rate_limiter.hpp"
#include "scanner.hpp"
#include "security.hpp"

#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

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
    try {
        atomic_write_file(path, body);
        return true;
    } catch (const std::exception &) {
        return false;
    }
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
