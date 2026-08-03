#include "App.hpp"
#include "./crypto/RustCrypto.hpp"
#include "./helpers/Helpers.hpp"
#include "./platform/Daemon.hpp"
#include "./platform/AppDataPaths.hpp"
#include "./platform/SecureStorage.hpp"
#include "ipc/IpcProtocolClient.hpp"
#include "./repo/UserRepo.hpp"
#include "./service/AuthService.hpp"
#include "./service/SyncService.hpp"
#include "./service/VaultService.hpp"
#include "./sync/RootScanner.hpp"
#include "./repo/ScanRepo.hpp"
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

namespace {

enum class Command {
  Login,
  Logout,
  SetBaseUrl,
  SyncAdd,
  SyncRun,
  Status,
  SecureStorageSmoke,
  Daemon,
  DaemonStop,
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

  if (command == "sync" && argc == 3 && std::string(argv[2]) == "run") {
    return Command::SyncRun;
  }

  if (command == "secure-storage-smoke" && argc == 2) {
    return Command::SecureStorageSmoke;
  }

  if (command == "daemon" && argc == 2) {
    return Command::Daemon;
  }

  if (command == "daemon" && argc == 3 && std::string(argv[2]) == "stop") {
    return Command::DaemonStop;
  }

  return Command::Unknown;
}

} // namespace

App::App(UserRepo &userRepo, AuthService *authService,
         VaultService &vaultService, SyncService &syncService,
         RootScanner &rootScanner, ScanRepo &scanRepo)
    : userRepo_(userRepo), authService_(authService),
      vaultService_(vaultService), syncService_(syncService),
      rootScanner_(rootScanner), scanRepo_(scanRepo) {}

App::~App() {}

int App::run(int argc, char *argv[]) {
  if (argc < 2) {
    spdlog::info("Usage: arkive-sync "
                 "<status|set-base-url|login|logout|sync|secure-storage-smoke|daemon>");
    return 0;
  }

  switch (parseCommand(argc, argv)) {
  case Command::SetBaseUrl: {
    const std::string newBaseUrl = argv[2];
    AccountRecord account{
        .baseUrl = newBaseUrl,
        .userId = std::nullopt,
        .email = std::nullopt,
        .vaultSalt = std::nullopt,
        .encryptedMasterKey = std::nullopt,
        .vaultSessionKeyId = std::nullopt,
        .vaultSessionBlob = std::nullopt,
    };

    if (const auto existingAccount = userRepo_.getAccount();
        existingAccount.has_value()) {
      if (existingAccount->baseUrl == newBaseUrl) {
        account.userId = existingAccount->userId;
        account.email = existingAccount->email;
        account.vaultSalt = existingAccount->vaultSalt;
        account.encryptedMasterKey = existingAccount->encryptedMasterKey;
        account.vaultSessionKeyId = existingAccount->vaultSessionKeyId;
        account.vaultSessionBlob = existingAccount->vaultSessionBlob;
      } else {
        vaultService_.clearPersistedSession();
      }
    }

    userRepo_.upsertAccount(account);
    spdlog::info("Base URL updated");
    return 0;
  }

  case Command::Login: {
    if (authService_ == nullptr) {
      throw std::runtime_error(
          "Base URL is not configured. Run: arkive-sync set-base-url <url>");
    }

    spdlog::info("Logging into arkive");
    const auto account = userRepo_.getAccount();
    if (!account.has_value()) {
      throw std::runtime_error("Base URL is missing");
    }

    const bool hasValidSession = authService_->hasValidSession();
    std::string password;

    if (hasValidSession) {
      password = readPasswordFromTerminal("Enter your vault password: ");
      if (!hasPersistedVaultMaterial(*account)) {
        authService_->refreshVaultMaterial(password);
      }
      vaultService_.unlock(password);
      spdlog::info("Session is already valid. Vault unlocked.");
      return 0;
    }

    std::string email;
    std::cout << "Enter your email: ";
    std::getline(std::cin, email);
    password = readPasswordFromTerminal("Enter your password: ");

    authService_->login(email, password);
    vaultService_.unlock(password);
    if (vaultService_.isUnlocked()) {
      spdlog::info("Successfully logged in and unlocked vault!");
    } else {
      spdlog::info("Successfully logged in!");
    }
    return 0;
  }

  case Command::Logout: {
    if (authService_ == nullptr) {
      throw std::runtime_error(
          "Base URL is not configured. Run: arkive-sync set-base-url <url>");
    }

    spdlog::info("Logging out of arkive");
    vaultService_.lock();
    vaultService_.clearPersistedSession();
    if (authService_->logout()) {
      spdlog::info("Successfully logged out!");
    } else {
      spdlog::info("No valid session found. Cleared local auth state.");
    }
    return 0;
  }

  case Command::SyncAdd: {
    const SyncRoot root = syncService_.addSyncRoot(argv[3]);
    spdlog::info("Added sync root id={} path={}", root.Id, root.localPath);
    return 0;
  }

  case Command::SyncRun: {
    const auto roots = syncService_.getSyncRoots();
    if (roots.empty()) {
      throw std::runtime_error("No sync roots configured");
    }

    size_t scanned = 0;
    for (const auto &root : roots) {
      if (!root.enabled) {
        continue;
      }

      while (true) {
        if (!rootScanner_.scanRoot(root.Id)) {
          throw std::runtime_error("Failed to scan sync root: " + root.Id);
        }

        if (!scanRepo_.hasRunningScanJob(root.Id)) {
          scanned++;
          break;
        }
      }
    }

    spdlog::info("Ran scan for {} sync root(s)", scanned);
    return 0;
  }

  case Command::Status: {
    try {
      arkive::ipc::Request request;
      request.set_protocol_version(ipc::kProtocolVersion);
      request.set_command(arkive::ipc::STATUS);
      const auto response =
          IpcProtocolClient(ipcEndpoint()).request(request);
      if (!response.ok()) {
        throw std::runtime_error(response.error());
      }
      spdlog::info("Arkive Sync engine is {} ({} sync root(s))",
                   response.state(), response.sync_root_count());
    } catch (const std::exception &) {
      spdlog::info("Arkive Sync is installed and working");
    }
    return 0;
  }

  case Command::DaemonStop: {
    arkive::ipc::Request request;
    request.set_protocol_version(ipc::kProtocolVersion);
    request.set_command(arkive::ipc::STOP);
    const auto response = IpcProtocolClient(ipcEndpoint()).request(request);
    if (!response.ok()) {
      throw std::runtime_error(response.error());
    }
    spdlog::info("Arkive Sync engine stopping");
    return 0;
  }

  case Command::SecureStorageSmoke: {
    RustCrypto crypto;
    auto storage = SecureStorage::create();
    const std::string service = "arkive-sync";
    const std::string account = "smoke-test:vault-session-key";
    const std::vector<uint8_t> secret = crypto.generateMasterKey();

    storage->storeSecret(service, account, secret);
    const auto loaded = storage->loadSecret(service, account);
    if (!loaded.has_value()) {
      throw std::runtime_error(
          "Secure storage smoke test failed: secret missing");
    }
    if (*loaded != secret) {
      throw std::runtime_error(
          "Secure storage smoke test failed: secret mismatch");
    }

    storage->deleteSecret(service, account);
    const auto deleted = storage->loadSecret(service, account);
    if (deleted.has_value()) {
      throw std::runtime_error("Secure storage smoke test failed: secret still "
                               "present after delete");
    }

    spdlog::info("Secure storage smoke test passed");
    return 0;
  }

  case Command::Daemon:
    return Daemon::create()->run();

  case Command::Unknown:
    spdlog::error(
        "Usage: arkive-sync login | arkive-sync logout | "
        "arkive-sync set-base-url <url> | arkive-sync status | "
        "arkive-sync sync add <path> | arkive-sync sync run | "
        "arkive-sync secure-storage-smoke | arkive-sync daemon [stop]");
    return 1;
  }

  return 1;
}
