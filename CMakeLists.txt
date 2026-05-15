cmake_minimum_required(VERSION 3.20)
project(obsidian_sync)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.7
)
FetchContent_MakeAvailable(cpp-httplib)

add_executable(obsidian-sync
    src/main.cpp
    src/db.cpp
    src/hasher.cpp
    src/scanner.cpp
    src/sync_plan.cpp
    src/config.cpp
    src/security.cpp
    src/sync_service.cpp
    src/local_remote_backend.cpp
    src/http_remote_backend.cpp
    src/output.cpp
    src/http_server.cpp
    src/manifest.cpp
    src/manifest_action.cpp
    src/three_way_compare.cpp
    src/two_way_compare.cpp
)

target_link_libraries(
    obsidian-sync
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    httplib::httplib
    sqlite3
)
