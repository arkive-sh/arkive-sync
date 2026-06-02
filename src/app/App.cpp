#include "App.hpp"
#include "./helpers/Helpers.hpp"
#include "./platform/SecureStorage.hpp"
#include "./repo/QueueRepo.hpp"
#include "./repo/SyncRepo.hpp"
#include "./repo/UserRepo.hpp"
#include "./crypto/RustCrypto.hpp"
#include "./service/AuthService.hpp"
#include "./service/QueueService.hpp"
#include "./service/SyncService.hpp"
#include "./service/UploadService.hpp"
#include "./service/VaultService.hpp"
#include <filesystem>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

namespace {

enum class Command {
  Login,
  Logout,
  SetBaseUrl,
  Status,
  SyncAdd,
  SyncRun,
  SyncRunAll,
  SyncList,
  SyncRemove,
  QueueStats,
  QueueProcess,
  QueueRetryFailed,
  QueueClearDone,
  SecureStorageSmoke,
  Daemon,
  Upload,
  Unknown,
};

Command parseCommand(int argc, char *argv[]) {
  if (argc < 2) {
    return Command::Unknown;
  }

  const std::string command = argv[1];

  if (command == "login" && argc == 2) {
    return Command::Login;
  }

  if (command == "status" && argc == 2) {
    return Command::Status;
  }

  if (command == "logout" && argc == 2) {
    return Command::Logout;
  }

  if (command == "set-base-url" && argc == 3) {
    return Command::SetBaseUrl;
  }

  if (command == "sync" && argc == 4 && std::string(argv[2]) == "add") {
    return Command::SyncAdd;
  }

  if (command == "sync" && argc == 4 && std::string(argv[2]) == "run") {
    return Command::SyncRun;
  }

  if (command == "sync" && argc == 3 && std::string(argv[2]) == "run-all") {
    return Command::SyncRunAll;
  }

  if (command == "sync" && argc == 3 && std::string(argv[2]) == "list") {
    return Command::SyncList;
  }

  if (command == "sync" && argc == 4 && std::string(argv[2]) == "remove") {
    return Command::SyncRemove;
  }

  if (command == "queue" && argc == 2) {
    return Command::QueueStats;
  }

  if (command == "queue" && argc == 3 && std::string(argv[2]) == "process") {
    return Command::QueueProcess;
  }

  if (command == "queue" && argc == 3 &&
      std::string(argv[2]) == "retry-failed") {
    return Command::QueueRetryFailed;
  }

  if (command == "queue" && argc == 3 && std::string(argv[2]) == "clear-done") {
    return Command::QueueClearDone;
  }

  if (command == "secure-storage-smoke" && argc == 2) {
    return Command::SecureStorageSmoke;
  }

  if (command == "daemon" && argc == 2) {
    return Command::Daemon;
  }

  if (command == "upload" && argc >= 3) {
    return Command::Upload;
  }

  return Command::Unknown;
}

} // namespace

App::App(UserRepo &userRepo, SyncRepo &syncRepo, QueueRepo &queueRepo,
         QueueService &queueService, SyncService &syncService,
         AuthService &authService, UploadService &uploadService,
         VaultService &vaultService)
    : userRepo_(userRepo), syncRepo_(syncRepo), queueRepo_(queueRepo),
      queueService_(queueService), syncService_(syncService),
      authService_(authService), uploadService_(uploadService),
      vaultService_(vaultService) {}

App::~App() {}

int App::run(int argc, char *argv[]) {
  if (argc < 2) {
    spdlog::info("Usage: arkive-sync "
                 "<status|set-base-url|login|logout|sync|queue|daemon|upload>");
    return 0;
  }

  switch (parseCommand(argc, argv)) {
  case Command::SetBaseUrl: {
    AccountRecord account{
        .baseUrl = argv[2],
        .email = std::nullopt,
        .vaultSalt = std::nullopt,
        .encryptedMasterKey = std::nullopt,
    };

    if (const auto existingAccount = userRepo_.getAccount();
        existingAccount.has_value()) {
      account.email = existingAccount->email;
      account.vaultSalt = existingAccount->vaultSalt;
      account.encryptedMasterKey = existingAccount->encryptedMasterKey;
    }

    userRepo_.upsertAccount(account);
    spdlog::info("Base URL updated");
    return 0;
  }

  case Command::Login: {
    spdlog::info("Logging into arkive");
    const auto account = userRepo_.getAccount();
    if (!account.has_value()) {
      throw std::runtime_error("Base URL is missing");
    }

    const bool hasValidSession = authService_.hasValidSession();
    std::string password;

    if (hasValidSession) {
      password = readPasswordFromTerminal("Enter your vault password: ");
      if (!hasPersistedVaultMaterial(*account)) {
        authService_.refreshVaultMaterial(password);
      }
      vaultService_.unlock(password);
      spdlog::info("Session is already valid. Vault unlocked.");
      return 0;
    }

    std::string email;
    std::cout << "Enter your email: ";
    std::getline(std::cin, email);
    password = readPasswordFromTerminal("Enter your password: ");

    authService_.login(email, password);
    vaultService_.unlock(password);
    if (vaultService_.isUnlocked()) {
      spdlog::info("Successfully logged in and unlocked vault!");
    } else {
      spdlog::info("Successfully logged in!");
    }
    return 0;
  }

  case Command::Logout: {
    spdlog::info("Logging out of arkive");
    vaultService_.lock();
    if (authService_.logout()) {
      spdlog::info("Successfully logged out!");
    } else {
      spdlog::info("No valid session found. Cleared local auth state.");
    }
    return 0;
  }

  case Command::Status:
    spdlog::info("Arkive Sync is installed and working");
    return 0;

  case Command::Upload: {
    const std::filesystem::path path =
        std::filesystem::absolute(argv[2]).lexically_normal();
    if (!std::filesystem::exists(path)) {
      throw std::runtime_error("Upload path does not exist");
    }
    if (!std::filesystem::is_regular_file(path)) {
      throw std::runtime_error("Upload path must be a file");
    }

    const auto fileSize = static_cast<int64_t>(std::filesystem::file_size(path));
    EntryRecord entry{
        .id = "manual-upload",
        .remoteId = std::nullopt,
        .syncRootId = "",
        .remoteType = "file",
        .localPath = path.filename().string(),
        .isDirectory = false,
        .parentFolderId = std::nullopt,
        .encryptedName = std::nullopt,
        .localSize = fileSize,
        .localMtime = std::nullopt,
        .localHash = std::nullopt,
        .remoteUpdatedAt = std::nullopt,
        .syncState = "pending_upload",
        .lastSyncedAt = std::nullopt,
    };

    const UploadFileResponse uploaded = uploadService_.uploadFile(path, entry);
    spdlog::info("Uploaded file: {}", path.string());
    spdlog::info("Remote file id: {}", uploaded.fileId);
    spdlog::info("Upload session id: {}", uploaded.uploadSessionId);
    return 0;
  }

  case Command::SyncAdd: {
    const std::filesystem::path syncPath = argv[3];
    syncService_.addPath(syncPath);
    spdlog::info("Added sync path: {}",
                 std::filesystem::absolute(syncPath).string());
    return 0;
  }

  case Command::SyncRun: {
    const std::filesystem::path syncPath = argv[3];
    const size_t insertedCount = syncService_.scanRoot(syncPath);
    spdlog::info("Ran sync for path: {}",
                 std::filesystem::absolute(syncPath).string());
    spdlog::info("Upserted {} entry records", insertedCount);
    return 0;
  }

  case Command::SyncRunAll:
    spdlog::info("sync run-all not implemented yet");
    return 0;

  case Command::SyncList:
    spdlog::info("sync list not implemented yet");
    return 0;

  case Command::SyncRemove:
    spdlog::info("sync remove not implemented yet");
    return 0;

  case Command::QueueStats: {
    const QueueStats stats = queueService_.stats();
    spdlog::info("Queue stats: queued={}, running={}, failed={}, done={}",
                 stats.queued, stats.running, stats.failed, stats.done);
  }
    return 0;

  case Command::QueueProcess:
    queueService_.processQueuedUploads();
    spdlog::info("Processed queued upload jobs");
    return 0;

  case Command::QueueRetryFailed: {
    queueService_.retryFailed();
    spdlog::info("Retried failed queue jobs");
  }
    return 0;

  case Command::QueueClearDone: {
    queueService_.clearDone();
    spdlog::info("Cleared done queue jobs");
  }
    return 0;

  case Command::SecureStorageSmoke: {
    RustCrypto crypto;
    auto storage = SecureStorage::create();
    const std::string service = "arkive-sync";
    const std::string account = "smoke-test:vault-session-key";
    const std::vector<uint8_t> secret = crypto.generateMasterKey();

    storage->storeSecret(service, account, secret);
    const auto loaded = storage->loadSecret(service, account);
    if (!loaded.has_value()) {
      throw std::runtime_error("Secure storage smoke test failed: secret missing");
    }
    if (*loaded != secret) {
      throw std::runtime_error(
          "Secure storage smoke test failed: secret mismatch");
    }

    storage->deleteSecret(service, account);
    const auto deleted = storage->loadSecret(service, account);
    if (deleted.has_value()) {
      throw std::runtime_error(
          "Secure storage smoke test failed: secret still present after delete");
    }

    spdlog::info("Secure storage smoke test passed");
    return 0;
  }

  case Command::Daemon:
    spdlog::info("daemon not implemented yet");
    return 0;

  case Command::Unknown:
    spdlog::error(
        "Usage: arkive-sync login | arkive-sync logout | "
        "arkive-sync set-base-url <url> | arkive-sync status | "
        "arkive-sync sync add <path> | arkive-sync sync run <path> | "
        "arkive-sync sync run-all | arkive-sync sync list | "
        "arkive-sync sync remove <path-or-id> | arkive-sync queue | "
        "arkive-sync queue process | arkive-sync queue retry-failed | "
        "arkive-sync queue clear-done | arkive-sync secure-storage-smoke | "
        "arkive-sync daemon | "
        "arkive-sync upload <path>");
    return 1;
  }

  return 1;
}
