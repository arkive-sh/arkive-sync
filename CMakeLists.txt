cmake_minimum_required(VERSION 3.25)

project(arkive-sync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ARKIVE_CRYPTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/arkive-crypto")

include(FetchContent)
find_package(PkgConfig REQUIRED)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
)

FetchContent_MakeAvailable(spdlog nlohmann_json Catch2)

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

add_executable(arkive-sync
    src/main.cpp
    src/app/App.cpp
    src/api/ArkiveApi.cpp
    src/api/ArkiveHttpClient.cpp
    src/db/Sqlite.cpp
    src/db/SqliteHelpers.cpp
    src/helpers/Helpers.cpp
    src/platform/SecureStorage.cpp
    src/repo/QueueRepo.cpp
    src/repo/SyncRepo.cpp
    src/repo/UserRepo.cpp
    src/service/QueueService.cpp
    src/service/SyncService.cpp
    src/service/AuthService.cpp
    src/service/UploadService.cpp
    src/service/VaultService.cpp
    src/crypto/Aad.cpp
    src/crypto/RustCrypto.cpp
    src/fs/ArkiveFileReader.cpp
    src/fs/ArkiveFileWriter.cpp
    src/fs/FileEncryptor.cpp
    src/fs/FileScanner.cpp
    src/fs/FileHasher.cpp
)

if(UNIX AND NOT APPLE)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
    target_include_directories(arkive-sync PRIVATE ${LIBSECRET_INCLUDE_DIRS})
    target_link_libraries(arkive-sync PRIVATE ${LIBSECRET_LIBRARIES})
    target_compile_options(arkive-sync PRIVATE ${LIBSECRET_CFLAGS_OTHER})
    target_sources(arkive-sync PRIVATE
        src/platform/linux/SecureStorage.cpp
    )
endif()

target_include_directories(arkive-sync PRIVATE
    src
    "${ARKIVE_CRYPTO_DIR}/include"
)

target_link_libraries(arkive-sync PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    CURL::libcurl
    SQLite3::SQLite3
    "${ARKIVE_CRYPTO_DIR}/lib/libarkive_crypto.a"
)

enable_testing()

add_executable(arkive-sync-tests
    tests/main_test.cpp
)

target_link_libraries(arkive-sync-tests PRIVATE
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(arkive-sync-tests)
