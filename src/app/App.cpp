#include "App.hpp"
#include "./api/ArkiveHttpClient.hpp"
#include "./db/Sqlite.hpp"
#include "./fs/FileScanner.hpp"
#include "./repo/SyncRepo.hpp"
#include "./repo/UserRepo.hpp"
#include "./service/AuthService.hpp"
#include "./service/SyncService.hpp"
#include <filesystem>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace {

static constexpr const char *kCookiePath =
    "/home/archnuman/.local/share/arkive-sync/cookies.txt";

enum class Command {
  Login,
  Logout,
  SetBaseUrl,
  Status,
  Upload,
  Sync,
  Unknown,
};

Command parseCommand(const std::string &command) {
  if (command == "login") {
    return Command::Login;
  }

  if (command == "status") {
    return Command::Status;
  }

  if (command == "logout") {
    return Command::Logout;
  }

  if (command == "set-base-url") {
    return Command::SetBaseUrl;
  }

  if (command == "upload") {
    return Command::Upload;
  }

  if (command == "sync") {
    return Command::Sync;
  }

  return Command::Unknown;
}

std::string requireBaseUrl(const UserRepo &userRepo) {
  const auto account = userRepo.getAccount();
  if (!account.has_value() || account->baseUrl.empty()) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }

  return account->baseUrl;
}

} // namespace

App::App() {}

App::~App() {}

int App::run(int argc, char *argv[]) {
  Database db;
  sqlite3 *dbInstance = db.getDb();
  UserRepo userRepo(dbInstance);
  if (argc < 2) {
    spdlog::info("Usage: arkive-sync "
                 "<status|set-base-url|login|logout|upload|download|sync>");
    return 0;
  }

  const std::string command = argv[1];

  switch (parseCommand(command)) {
  case Command::SetBaseUrl: {
    if (argc < 3) {
      spdlog::error("Usage: arkive-sync set-base-url <url>");
      return 1;
    }

    AccountRecord account{
        .baseUrl = argv[2],
        .email = std::nullopt,
        .vaultSalt = std::nullopt,
        .encryptedMasterKey = std::nullopt,
    };

    if (const auto existingAccount = userRepo.getAccount();
        existingAccount.has_value()) {
      account.email = existingAccount->email;
      account.vaultSalt = existingAccount->vaultSalt;
      account.encryptedMasterKey = existingAccount->encryptedMasterKey;
    }

    userRepo.upsertAccount(account);
    spdlog::info("Base URL updated");
    return 0;
  }

  case Command::Login: {
    spdlog::info("Logging into arkive");

    const std::string baseUrl = requireBaseUrl(userRepo);
    ArkiveHttpClient client(baseUrl, kCookiePath);
    AuthService authService(userRepo, client);
    if (authService.login()) {
      spdlog::info("Successfully logged in!");
    } else {
      spdlog::info("Session is already valid. Skipping login.");
    }
    return 0;
  }

  case Command::Logout: {
    spdlog::info("Logging out of arkive");

    const std::string baseUrl = requireBaseUrl(userRepo);
    ArkiveHttpClient client(baseUrl, kCookiePath);
    AuthService authService(userRepo, client);
    if (authService.logout()) {
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
    if (argc < 3) {
      spdlog::error("Usage: arkive-sync upload <path>");
      return 1;
    }

    std::string path = argv[2];
    spdlog::info("Upload requested for: {}", path);
    return 0;
  }

  case Command::Sync: {
    if (argc == 5 && std::string(argv[2]) == "add" &&
        std::string(argv[3]) == "path") {
      const std::filesystem::path syncPath = argv[4];
      FileScanner fileScanner(syncPath);
      SyncRepo syncRepo(dbInstance);
      SyncService syncService(syncRepo, fileScanner);
      const size_t insertedCount = syncService.addPath();
      spdlog::info("Added sync path: {}",
                   std::filesystem::absolute(syncPath).string());
      spdlog::info("Inserted {} entry records", insertedCount);
      return 0;
    }

    spdlog::error("Usage: arkive-sync sync add path <path>");
    return 1;
  }

  case Command::Unknown:
    spdlog::error("Unknown command: {}", command);
    return 1;
  }

  return 1;
}
