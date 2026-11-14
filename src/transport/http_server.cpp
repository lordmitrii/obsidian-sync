#include "http_server.hpp"

#include "access_log.hpp"
#include "atomic_file.hpp"
#include "metrics.hpp"
#include "output.hpp"
#include "rate_limiter.hpp"
#include "scanner.hpp"
#include "security.hpp"
#include "time_utils.hpp"
#include "version_history.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

static constexpr std::size_t kStreamChunkSize = 64 * 1024;

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

static std::string rate_limit_key(const httplib::Request &req, const std::string &trusted_proxy) {
    if (!trusted_proxy.empty() && req.remote_addr == trusted_proxy && req.has_header("X-Real-IP")) {
        return req.get_header_value("X-Real-IP");
    }

    return req.remote_addr;
}

static const char *kRequestIdHeader = "X-Osync-Request-Id";
static const char *kRequestStartHeader = "X-Osync-Request-Start-Ns";

static void stamp_request_metadata(const httplib::Request &req,
                                   std::atomic<std::uint64_t> &next_request_id) {
    auto &mutable_req = const_cast<httplib::Request &>(req);
    std::uint64_t id = next_request_id.fetch_add(1);
    auto start_ns = std::chrono::steady_clock::now().time_since_epoch().count();
    mutable_req.set_header(kRequestIdHeader, std::to_string(id));
    mutable_req.set_header(kRequestStartHeader, std::to_string(start_ns));
}

static void log_access(const httplib::Request &req, const httplib::Response &res,
                       ServerMetrics &metrics) {
    metrics.record_request(res.status, res.body.size());

    AccessLogEntry entry;
    entry.timestamp = current_timestamp_iso8601();
    entry.method = req.method;
    entry.path = req.path;
    entry.status = res.status;
    entry.response_bytes = res.body.size();
    entry.remote_addr = req.remote_addr;

    if (req.has_header(kRequestIdHeader)) {
        entry.request_id = std::stoull(req.get_header_value(kRequestIdHeader));
    }

    if (req.has_header(kRequestStartHeader)) {
        auto start_ns = std::stoll(req.get_header_value(kRequestStartHeader));
        auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        entry.duration_ms = static_cast<double>(now_ns - start_ns) / 1'000'000.0;
    }

    std::cout << format_access_log_entry(entry) << std::endl;
}

void run_http_server(const fs::path &server_root, const std::string &host, int port,
                     const std::string &bearer_token, std::size_t max_upload_bytes,
                     int rate_limit_per_minute, const std::string &trusted_proxy,
                     int trash_retention_days, std::size_t max_versions_per_file,
                     int version_retention_days, SyncConfigPreset sync_config) {
    httplib::Server server;
    RateLimiter authenticated_limiter(rate_limit_per_minute);
    RateLimiter unauthenticated_limiter(rate_limit_per_minute);
    std::atomic<std::uint64_t> next_request_id{1};
    ServerMetrics metrics;
    VersionHistory versions(server_root / ".versions");

    server.set_payload_max_length(max_upload_bytes);
    server.set_logger([&metrics](const httplib::Request &req, const httplib::Response &res) {
        log_access(req, res, metrics);
    });
    server.set_pre_routing_handler(
        [&bearer_token, &authenticated_limiter, &unauthenticated_limiter, &trusted_proxy,
         &next_request_id](const httplib::Request &req, httplib::Response &res) {
            stamp_request_metadata(req, next_request_id);

            if (req.path == "/healthz") {
                return httplib::Server::HandlerResponse::Unhandled;
            }

            std::string key = rate_limit_key(req, trusted_proxy);
            bool authorized = is_authorized(req, bearer_token);
            RateLimiter &limiter = authorized ? authenticated_limiter : unauthenticated_limiter;

            if (!limiter.allow(key)) {
                res.status = 429;
                res.set_content("Too many requests\n", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }

            if (!authorized) {
                res.status = 401;
                res.set_content("Unauthorized\n", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }

            return httplib::Server::HandlerResponse::Unhandled;
        });

    server.Get("/healthz", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("{\"status\":\"ok\"}\n", "application/json");
    });

    server.Get("/metrics", [&metrics](const httplib::Request &, httplib::Response &res) {
        res.set_content(format_metrics_prometheus(metrics.snapshot()), "text/plain; version=0.0.4");
    });

    // Reused across requests so GET /manifest can skip rehashing files whose
    // size and mtime haven't changed since the last request, the same cache
    // the client keeps in state.db (see scan_file's known_hashes). Guarded
    // by a mutex since httplib serves requests from a thread pool; server
    // and both caches below live for the whole call, same as the rate
    // limiters captured by reference further up.
    Manifest manifest_cache;
    std::mutex manifest_cache_mutex;

    server.Get("/manifest", [server_root, &manifest_cache, &manifest_cache_mutex,
                             sync_config](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(manifest_cache_mutex);

        auto files = scan_vault(server_root, manifest_cache, sync_config);

        manifest_cache.clear();
        for (const auto &file : files) {
            manifest_cache[file.path] = file;
        }

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

        auto file = std::make_shared<std::ifstream>(full_path, std::ios::binary);

        if (!*file) {
            res.status = 500;
            res.set_content("Failed to read file\n", "text/plain");
            return;
        }

        res.set_content_provider(
            fs::file_size(full_path), "application/octet-stream",
            [file](std::size_t offset, std::size_t length, httplib::DataSink &sink) {
                std::vector<char> buffer(std::min(length, kStreamChunkSize));
                file->seekg(static_cast<std::streamoff>(offset));
                file->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                auto read = static_cast<std::size_t>(file->gcount());

                if (read == 0) {
                    return false;
                }

                return sink.write(buffer.data(), read);
            });
    });

    server.Put("/file", [server_root, &versions, max_versions_per_file, version_retention_days](
                            const httplib::Request &req, httplib::Response &res,
                            const httplib::ContentReader &content_reader) {
        std::string relative_path;

        if (!request_path(req, relative_path)) {
            content_reader([](const char *, std::size_t) { return true; });
            res.status = 400;
            res.set_content("Invalid path\n", "text/plain");
            return;
        }

        fs::path full_path = server_root / fs::path(relative_path);
        std::unique_ptr<AtomicFileWriter> writer;
        std::string write_error;

        try {
            writer = std::make_unique<AtomicFileWriter>(full_path);
        } catch (const std::exception &e) {
            write_error = e.what();
        }

        bool read_ok = content_reader([&](const char *data, std::size_t length) {
            if (!write_error.empty()) {
                return true;
            }

            try {
                writer->write(data, length);
            } catch (const std::exception &e) {
                write_error = e.what();
            }

            return true;
        });

        if (!read_ok) {
            return;
        }

        if (!write_error.empty()) {
            res.status = 500;
            res.set_content("Failed to write file\n", "text/plain");
            return;
        }

        std::string previous_content;
        bool had_previous = fs::exists(full_path) && read_file(full_path, previous_content);

        try {
            writer->commit();
        } catch (const std::exception &) {
            res.status = 500;
            res.set_content("Failed to write file\n", "text/plain");
            return;
        }

        if (had_previous) {
            versions.record(relative_path, previous_content);
            versions.prune_all(max_versions_per_file, version_retention_days);
        }

        res.status = 204;
    });

    server.Delete("/file",
                  [server_root, trash_retention_days, &versions, max_versions_per_file,
                   version_retention_days](const httplib::Request &req, httplib::Response &res) {
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

                      if (fs::exists(full_path)) {
                          std::string content;

                          if (read_file(full_path, content)) {
                              versions.record(relative_path, content);
                              versions.prune_all(max_versions_per_file, version_retention_days);
                          }

                          move_to_trash(full_path, server_root / ".trash", fs::path(relative_path));
                          cleanup_trash(server_root / ".trash", trash_retention_days);
                      }

                      res.status = 204;
                  });

    server.Get("/history", [&versions](const httplib::Request &req, httplib::Response &res) {
        std::string relative_path;

        if (!request_path(req, relative_path)) {
            res.status = 400;
            res.set_content("Invalid path\n", "text/plain");
            return;
        }

        nlohmann::json j = nlohmann::json::array();

        for (const auto &version : versions.list(relative_path)) {
            j.push_back(
                {{"hash", version.hash}, {"timestamp", version.timestamp}, {"size", version.size}});
        }

        res.set_content(j.dump(2), "application/json");
    });

    server.Post("/restore",
                [server_root, &versions](const httplib::Request &req, httplib::Response &res) {
                    std::string relative_path;

                    if (!request_path(req, relative_path)) {
                        res.status = 400;
                        res.set_content("Invalid path\n", "text/plain");
                        return;
                    }

                    if (!req.has_param("hash")) {
                        res.status = 400;
                        res.set_content("Missing hash parameter\n", "text/plain");
                        return;
                    }

                    auto restored_content = versions.content(req.get_param_value("hash"));

                    if (!restored_content) {
                        res.status = 404;
                        res.set_content("Version not found\n", "text/plain");
                        return;
                    }

                    fs::path full_path = server_root / fs::path(relative_path);
                    std::string current_content;

                    if (fs::exists(full_path) && read_file(full_path, current_content)) {
                        versions.record(relative_path, current_content);
                    }

                    if (!write_file(full_path, *restored_content)) {
                        res.status = 500;
                        res.set_content("Failed to write file\n", "text/plain");
                        return;
                    }

                    res.status = 204;
                });

    server.Post("/prune", [&versions, max_versions_per_file, version_retention_days](
                              const httplib::Request &, httplib::Response &res) {
        versions.prune_all(max_versions_per_file, version_retention_days);
        res.status = 204;
    });

    std::cout << "Serving " << server_root << " on http://" << host << ":" << port << "\n";
    std::cout << "Max upload size: " << max_upload_bytes << " bytes\n";
    std::cout << "Rate limit: " << rate_limit_per_minute << " requests/minute\n";

    if (!server.listen(host, port)) {
        throw std::runtime_error("Failed to start HTTP server");
    }
}
