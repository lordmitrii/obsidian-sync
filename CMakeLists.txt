cmake_minimum_required(VERSION 3.20)
project(obsidian_sync)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

add_executable(obsidian-sync
    src/main.cpp
    src/db.cpp
    src/hasher.cpp
    src/scanner.cpp
    src/sync_plan.cpp
    src/config.cpp
    src/output.cpp
)

target_link_libraries(
    obsidian-sync
    OpenSSL::SSL
    OpenSSL::Crypto
    sqlite3
)