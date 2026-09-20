cmake_minimum_required(VERSION 3.20)
project(obsidian_sync)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

include(FetchContent)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.7
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(cpp-httplib nlohmann_json)

add_library(obsidian-sync-core
    src/core/config.cpp
    src/core/db.cpp
    src/core/hasher.cpp
    src/core/manifest.cpp
    src/core/manifest_action.cpp
    src/core/output.cpp
    src/core/scanner.cpp
    src/core/security.cpp
    src/sync/http_remote_backend.cpp
    src/sync/local_remote_backend.cpp
    src/sync/sync_plan.cpp
    src/sync/sync_service.cpp
    src/sync/three_way_compare.cpp
    src/sync/two_way_compare.cpp
    src/transport/http_server.cpp
)

target_link_libraries(
    obsidian-sync-core
    PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    httplib::httplib
    nlohmann_json::nlohmann_json
    sqlite3
)

target_include_directories(
    obsidian-sync-core
    PUBLIC
    src/core
    src/sync
    src/transport
)

add_executable(obsidian-sync-client src/app/client_main.cpp)
target_link_libraries(obsidian-sync-client PRIVATE obsidian-sync-core)

add_executable(obsidian-sync-server src/app/server_main.cpp)
target_link_libraries(obsidian-sync-server PRIVATE obsidian-sync-core)

enable_testing()

add_executable(unit-tests tests/unit/test_compare.cpp)
target_link_libraries(unit-tests PRIVATE obsidian-sync-core)
add_test(NAME unit-tests COMMAND unit-tests)

add_executable(scanner-tests tests/unit/test_scanner.cpp)
target_link_libraries(scanner-tests PRIVATE obsidian-sync-core)
add_test(NAME scanner-tests COMMAND scanner-tests)

add_executable(config-tests tests/unit/test_config.cpp)
target_link_libraries(config-tests PRIVATE obsidian-sync-core)
add_test(NAME config-tests COMMAND config-tests)

add_executable(security-tests tests/unit/test_security.cpp)
target_link_libraries(security-tests PRIVATE obsidian-sync-core)
add_test(NAME security-tests COMMAND security-tests)

add_executable(manifest-tests tests/unit/test_manifest.cpp)
target_link_libraries(manifest-tests PRIVATE obsidian-sync-core)
add_test(NAME manifest-tests COMMAND manifest-tests)

add_executable(plan-tests tests/unit/test_plans.cpp)
target_link_libraries(plan-tests PRIVATE obsidian-sync-core)
add_test(NAME plan-tests COMMAND plan-tests)

add_executable(hasher-tests tests/unit/test_hasher.cpp)
target_link_libraries(hasher-tests PRIVATE obsidian-sync-core)
add_test(NAME hasher-tests COMMAND hasher-tests)

add_executable(output-tests tests/unit/test_output.cpp)
target_link_libraries(output-tests PRIVATE obsidian-sync-core)
add_test(NAME output-tests COMMAND output-tests)

add_executable(db-tests tests/unit/test_db.cpp)
target_link_libraries(db-tests PRIVATE obsidian-sync-core)
add_test(NAME db-tests COMMAND db-tests)

add_executable(local-backend-tests tests/unit/test_local_backend.cpp)
target_link_libraries(local-backend-tests PRIVATE obsidian-sync-core)
add_test(NAME local-backend-tests COMMAND local-backend-tests)
