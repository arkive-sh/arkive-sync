cmake_minimum_required(VERSION 3.25)

project(arkive-sync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ARKIVE_CRYPTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/arkive-crypto")
set(ARKIVE_CRYPTO_FFI_MANIFEST
    "${ARKIVE_CRYPTO_DIR}/crates/arkive_crypto_ffi/Cargo.toml"
)
set(ARKIVE_CRYPTO_TARGET_DIR "${CMAKE_BINARY_DIR}/arkive-crypto-target")

if(NOT EXISTS "${ARKIVE_CRYPTO_DIR}/Cargo.toml")
    message(FATAL_ERROR
        "arkive-crypto source is missing. Run `git submodule update --init --recursive`."
    )
endif()

find_program(CARGO_EXECUTABLE cargo REQUIRED)

if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(ARKIVE_CRYPTO_STATIC_LIB
        "${ARKIVE_CRYPTO_TARGET_DIR}/release/arkive_crypto.lib"
    )
else()
    set(ARKIVE_CRYPTO_STATIC_LIB
        "${ARKIVE_CRYPTO_TARGET_DIR}/release/libarkive_crypto.a"
    )
endif()

file(GLOB_RECURSE ARKIVE_CRYPTO_SOURCES CONFIGURE_DEPENDS
    "${ARKIVE_CRYPTO_DIR}/Cargo.toml"
    "${ARKIVE_CRYPTO_DIR}/Cargo.lock"
    "${ARKIVE_CRYPTO_DIR}/include/*.h"
    "${ARKIVE_CRYPTO_DIR}/crates/*/Cargo.toml"
    "${ARKIVE_CRYPTO_DIR}/crates/*/src/*.rs"
)

add_custom_command(
    OUTPUT "${ARKIVE_CRYPTO_STATIC_LIB}"
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CARGO_TARGET_DIR=${ARKIVE_CRYPTO_TARGET_DIR}"
        "${CARGO_EXECUTABLE}" build
        --manifest-path "${ARKIVE_CRYPTO_FFI_MANIFEST}"
        --release
    WORKING_DIRECTORY "${ARKIVE_CRYPTO_DIR}"
    DEPENDS ${ARKIVE_CRYPTO_SOURCES}
    COMMENT "Building arkive-crypto native library"
    VERBATIM
)

add_custom_target(arkive-crypto-build
    DEPENDS "${ARKIVE_CRYPTO_STATIC_LIB}"
)

add_library(arkive_crypto_native STATIC IMPORTED GLOBAL)
set_target_properties(arkive_crypto_native PROPERTIES
    IMPORTED_LOCATION "${ARKIVE_CRYPTO_STATIC_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${ARKIVE_CRYPTO_DIR}/include"
)
add_dependencies(arkive_crypto_native arkive-crypto-build)

include(FetchContent)

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
    src/api/HttpError.cpp
    src/api/ArkiveHttpClient.cpp
    src/db/Sqlite.cpp
    src/db/SqliteHelpers.cpp
    src/download/DownloadManifest.cpp
    src/download/DownloadRecordDecryptor.cpp
    src/download/DownloadService.cpp
    src/helpers/Base64.cpp
    src/helpers/GenUUID.cpp
    src/helpers/Helpers.cpp
    src/helpers/Hex.cpp
    src/helpers/Mime.cpp
    src/fs/helpers/PathHelpers.cpp
    src/platform/AppDataPaths.cpp
    src/platform/AtomicFileWriterFactory.cpp
    src/platform/Daemon.cpp
    src/platform/daemon/PollingDaemon.cpp
    src/platform/SecureStorage.cpp
    src/repo/ConflictRepo.cpp
    src/repo/DirtyPathRepo.cpp
    src/repo/EntryRepo.cpp
    src/repo/LocalEntryRepo.cpp
    src/repo/QueueRepo.cpp
    src/repo/RemoteEntryRepo.cpp
    src/repo/ScanRepo.cpp
    src/repo/SyncRepo.cpp
    src/repo/UploadResumeRepo.cpp
    src/repo/UserRepo.cpp
    src/service/AuthService.cpp
    src/service/UploadJobRunner.cpp
    src/service/UploadService.cpp
    src/service/QueueService.cpp
    src/service/RemoteSyncService.cpp
    src/service/FolderCreateWorker.cpp
    src/service/SyncReconciler.cpp
    src/service/SyncService.cpp
    src/service/VaultService.cpp
    src/sync/RemoteScanner.cpp
    src/sync/RootScanner.cpp
    src/sync/SyncMode.cpp
    src/sync/SyncPolicy.cpp
    src/upload/UploadPolicy.cpp
    src/upload/UploadPlanner.cpp
    src/upload/MultipartUploader.cpp
    src/upload/UploadFinalizer.cpp
    src/crypto/Aad.cpp
    src/crypto/RustCrypto.cpp
    src/fs/UploadFileChunkReader.cpp
    src/fs/DecryptedFileWriter.cpp
    src/fs/FileEncryptor.cpp
    src/fs/FileWatcher.cpp
    src/fs/FileHasher.cpp
    src/fs/FileSnapshot.cpp
)

if(APPLE)
    target_sources(arkive-sync PRIVATE
        src/platform/macos/filewriter/MacosAtomicFileWriter.cpp
        src/platform/macos/watcher/FSEventsWatcher.cpp
        src/platform/macos/SecureStorage.cpp
    )
    target_link_libraries(arkive-sync PRIVATE
        "-framework CoreFoundation"
        "-framework CoreServices"
        "-framework Security"
    )
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
    target_include_directories(arkive-sync PRIVATE ${LIBSECRET_INCLUDE_DIRS})
    target_link_libraries(arkive-sync PRIVATE ${LIBSECRET_LIBRARIES})
    target_compile_options(arkive-sync PRIVATE ${LIBSECRET_CFLAGS_OTHER})
    target_sources(arkive-sync PRIVATE
        src/platform/linux/daemon/LinuxDaemon.cpp
        src/platform/linux/filewriter/LinuxAtomicFileWriter.cpp
        src/platform/linux/SecureStorage.cpp
    )
elseif(WIN32)
    target_sources(arkive-sync PRIVATE
        src/platform/windows/filewriter/WindowsAtomicFileWriter.cpp
        src/platform/windows/watcher/ReadDirectoryChangesWatcher.cpp
        src/platform/windows/SecureStorage.cpp
    )
    target_link_libraries(arkive-sync PRIVATE Advapi32)
endif()

target_include_directories(arkive-sync PRIVATE
    src
    "${ARKIVE_CRYPTO_DIR}/include"
)

target_compile_definitions(arkive-sync PRIVATE
    ARKIVE_SYNC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(arkive-sync PRIVATE
    arkive_crypto_native
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    CURL::libcurl
    SQLite3::SQLite3
)

enable_testing()

add_executable(arkive-sync-tests
    src/api/ArkiveApi.cpp
    src/api/ArkiveHttpClient.cpp
    src/api/HttpError.cpp
    src/db/Sqlite.cpp
    src/download/DownloadManifest.cpp
    src/download/DownloadRecordDecryptor.cpp
    src/download/DownloadService.cpp
    src/fs/UploadFileChunkReader.cpp
    src/fs/FileEncryptor.cpp
    src/fs/FileWatcher.cpp
    src/fs/FileSnapshot.cpp
    src/platform/AppDataPaths.cpp
    src/platform/AtomicFileWriterFactory.cpp
    src/platform/linux/filewriter/LinuxAtomicFileWriter.cpp
    src/sync/RemoteScanner.cpp
    src/service/RemoteSyncService.cpp
    tests/arkive_api_test.cpp
    tests/download_manifest_test.cpp
    tests/download_service_test.cpp
    tests/file_snapshot_test.cpp
    tests/inotify_watcher_test.cpp
    tests/local_entry_repo_test.cpp
    tests/folder_create_worker_test.cpp
    tests/queue_repo_test.cpp
    tests/secure_storage_stub.cpp
    tests/sync_policy_test.cpp
    tests/sync_reconciler_test.cpp
    tests/upload_job_runner2_test.cpp
    tests/upload_planner_test.cpp
    tests/vault_service_test.cpp
    src/crypto/Aad.cpp
    src/crypto/RustCrypto.cpp
    src/db/SqliteHelpers.cpp
    src/helpers/Base64.cpp
    src/helpers/GenUUID.cpp
    src/helpers/Hex.cpp
    src/helpers/Mime.cpp
    src/fs/helpers/PathHelpers.cpp
    src/fs/FileHasher.cpp
    src/repo/ConflictRepo.cpp
    src/repo/DirtyPathRepo.cpp
    src/repo/EntryRepo.cpp
    src/repo/LocalEntryRepo.cpp
    src/repo/QueueRepo.cpp
    src/repo/RemoteEntryRepo.cpp
    src/repo/ScanRepo.cpp
    src/repo/SyncRepo.cpp
    src/repo/UploadResumeRepo.cpp
    src/repo/UserRepo.cpp
    src/service/FolderCreateWorker.cpp
    src/service/UploadJobRunner.cpp
    src/service/QueueService.cpp
    src/service/SyncReconciler.cpp
    src/service/SyncService.cpp
    src/service/VaultService.cpp
    src/sync/RootScanner.cpp
    src/sync/SyncMode.cpp
    src/sync/SyncPolicy.cpp
    src/upload/MultipartUploader.cpp
    src/upload/UploadPlanner.cpp
    src/upload/UploadPolicy.cpp
)

target_link_libraries(arkive-sync-tests PRIVATE
    arkive_crypto_native
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CURL::libcurl
    SQLite3::SQLite3
)

target_include_directories(arkive-sync-tests PRIVATE
    src
    "${ARKIVE_CRYPTO_DIR}/include"
)

target_compile_definitions(arkive-sync-tests PRIVATE
    ARKIVE_SYNC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS arkive-sync-tests
    COMMENT "Running unit tests..."
)

include(CTest)
include(Catch)
catch_discover_tests(arkive-sync-tests)
