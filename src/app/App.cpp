#include "App.hpp"
#include "./crypto/RustCrypto.hpp"
#include "./helpers/Helpers.hpp"
#include "./platform/Daemon.hpp"
#include "./platform/SecureStorage.hpp"
#include "./repo/UserRepo.hpp"
#include "./service/AuthService.hpp"
#include "./service/VaultService.hpp"
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
  SecureStorageSmoke,
  Daemon,
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

  if (command == "secure-storage-smoke" && argc == 2) {
    return Command::SecureStorageSmoke;
  }

  if (command == "daemon" && argc == 2) {
    return Command::Daemon;
  }

  return Command::Unknown;
}

} // namespace

App::App(UserRepo &userRepo, AuthService &authService,
         VaultService &vaultService)
    : userRepo_(userRepo), authService_(authService),
      vaultService_(vaultService) {}

App::~App() {}

int App::run(int argc, char *argv[]) {
  if (argc < 2) {
    spdlog::info("Usage: arkive-sync "
                 "<status|set-base-url|login|logout|secure-storage-smoke|daemon>");
    return 0;
  }

  switch (parseCommand(argc, argv)) {
  case Command::SetBaseUrl: {
    const std::string newBaseUrl = argv[2];
    AccountRecord account{
        .baseUrl = newBaseUrl,
        .email = std::nullopt,
        .vaultSalt = std::nullopt,
        .encryptedMasterKey = std::nullopt,
        .vaultSessionKeyId = std::nullopt,
        .vaultSessionBlob = std::nullopt,
    };

    if (const auto existingAccount = userRepo_.getAccount();
        existingAccount.has_value()) {
      if (existingAccount->baseUrl == newBaseUrl) {
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
    vaultService_.clearPersistedSession();
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
        "arkive-sync secure-storage-smoke | arkive-sync daemon");
    return 1;
  }

  return 1;
}
