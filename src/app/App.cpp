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
#include <cstdlib>
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
  SyncList,
  SyncRemove,
  SyncPull,
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

  if (command == "sync" && argc == 3 && std::string(argv[2]) == "list") {
    return Command::SyncList;
  }

  if (command == "sync" && argc == 4 && std::string(argv[2]) == "remove") {
    return Command::SyncRemove;
  }

  if (command == "sync" && argc == 3 && std::string(argv[2]) == "pull") {
    return Command::SyncPull;
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
    IpcProtocolClient client(ipcEndpoint());
    arkive::ipc::Request statusRequest;
    statusRequest.set_protocol_version(ipc::kProtocolVersion);
    statusRequest.set_command(arkive::ipc::STATUS);
    const auto status = client.request(statusRequest);
    if (!status.ok()) {
      throw std::runtime_error(status.error());
    }

    arkive::ipc::Request loginRequest;
    loginRequest.set_protocol_version(ipc::kProtocolVersion);
    loginRequest.set_command(arkive::ipc::LOGIN);
    const char *environmentEmail = std::getenv("ARKIVE_LOGIN_EMAIL");
    const char *environmentPassword = std::getenv("ARKIVE_LOGIN_PASSWORD");
    if (environmentEmail != nullptr && environmentPassword != nullptr) {
      loginRequest.set_email(environmentEmail);
      loginRequest.set_password(environmentPassword);
    } else if (status.authenticated()) {
      loginRequest.set_password(
          readPasswordFromTerminal("Enter your vault password: "));
    } else {
      std::string email;
      std::cout << "Enter your email: ";
      std::getline(std::cin, email);
      loginRequest.set_email(email);
      loginRequest.set_password(
          readPasswordFromTerminal("Enter your password: "));
    }

    const auto response = client.request(loginRequest);
    if (!response.ok()) {
      throw std::runtime_error(response.error());
    }
    if (response.vault_unlocked()) {
      spdlog::info("Successfully logged in and unlocked vault!");
    } else {
      spdlog::info("Successfully logged in!");
    }
    return 0;
  }

  case Command::Logout: {
    arkive::ipc::Request request;
    request.set_protocol_version(ipc::kProtocolVersion);
    request.set_command(arkive::ipc::LOGOUT);
    const auto response = IpcProtocolClient(ipcEndpoint()).request(request);
    if (!response.ok()) {
      throw std::runtime_error(response.error());
    }
    spdlog::info("Successfully logged out!");
    return 0;
  }

  case Command::SyncAdd: {
    arkive::ipc::Request request;
    request.set_protocol_version(ipc::kProtocolVersion);
    request.set_command(arkive::ipc::SYNC_ADD);
    request.set_path(argv[3]);
    const auto response = IpcProtocolClient(ipcEndpoint()).request(request);
    if (!response.ok()) {
      throw std::runtime_error(response.error());
    }
    spdlog::info("Added sync root id={} path={}", response.sync_root_id(),
                 response.sync_root_path());
    return 0;
  }

  case Command::SyncRun: {
    arkive::ipc::Request request;
    request.set_protocol_version(ipc::kProtocolVersion);
    request.set_command(arkive::ipc::SYNC_RUN);
    const auto response = IpcProtocolClient(ipcEndpoint()).request(request);
    if (!response.ok()) {
      throw std::runtime_error(response.error());
    }
    spdlog::info("Ran scan for {} sync root(s)",
                 response.scanned_root_count());
    spdlog::info("{} remote entr{} available",
                 response.synced_entry_count(),
                 response.synced_entry_count() == 1 ? "y" : "ies");
    return 0;
  }

  case Command::SyncList: {
    arkive::ipc::Request request;
    request.set_protocol_version(ipc::kProtocolVersion);
    request.set_command(arkive::ipc::SYNC_LIST);
    const auto response = IpcProtocolClient(ipcEndpoint()).request(request);
    if (!response.ok()) {
      throw std::runtime_error(response.error());
    }
    for (const auto &root : response.sync_roots()) {
      spdlog::info("Sync root id={} path={} enabled={} mode={}", root.id(),
                   root.path(), root.enabled(), root.mode());
    }
    if (response.sync_roots().empty()) {
      spdlog::info("No sync roots configured");
    }
    return 0;
  }

  case Command::SyncRemove: {
    arkive::ipc::Request request;
    request.set_protocol_version(ipc::kProtocolVersion);
    request.set_command(arkive::ipc::SYNC_REMOVE);
    request.set_path(argv[3]);
    const auto response = IpcProtocolClient(ipcEndpoint()).request(request);
    if (!response.ok()) {
      throw std::runtime_error(response.error());
    }
    spdlog::info("Disabled sync root {}", response.sync_root_id());
    return 0;
  }

  case Command::SyncPull: {
    arkive::ipc::Request request;
    request.set_protocol_version(ipc::kProtocolVersion);
    request.set_command(arkive::ipc::SYNC_PULL);
    const auto response = IpcProtocolClient(ipcEndpoint()).request(request);
    if (!response.ok()) {
      throw std::runtime_error(response.error());
    }
    spdlog::info("Pulled remote changes for {} sync root(s)",
                 response.scanned_root_count());
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
        "arkive-sync sync add <path> | arkive-sync sync list | "
        "arkive-sync sync remove <id> | arkive-sync sync run | "
        "arkive-sync sync pull | "
        "arkive-sync secure-storage-smoke | arkive-sync daemon [stop]");
    return 1;
  }

  return 1;
}
