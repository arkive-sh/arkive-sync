#include "App.hpp"
#include "./api/ArkiveHttpClient.hpp"
#include "./db/Sqlite.hpp"
#include "./fs/FileScanner.hpp"
#include "./repo/QueueRepo.hpp"
#include "./repo/SyncRepo.hpp"
#include "./repo/UserRepo.hpp"
#include "./service/AuthService.hpp"
#include "./service/QueueService.hpp"
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
  SyncAdd,
  SyncRun,
  SyncRunAll,
  SyncList,
  SyncRemove,
  QueueStats,
  QueueProcess,
  QueueRetryFailed,
  QueueClearDone,
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

  if (command == "queue" && argc == 3 &&
      std::string(argv[2]) == "clear-done") {
    return Command::QueueClearDone;
  }

  if (command == "daemon" && argc == 2) {
    return Command::Daemon;
  }

  if (command == "upload" && argc >= 3) {
    return Command::Upload;
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
    std::string path = argv[2];
    spdlog::info("Upload requested for: {}", path);
    return 0;
  }

  case Command::SyncAdd: {
    const std::filesystem::path syncPath = argv[3];
    FileScanner fileScanner(syncPath);
    QueueRepo queueRepo(dbInstance);
    SyncRepo syncRepo(dbInstance);
    SyncService syncService(syncRepo, queueRepo, fileScanner);
    syncService.addPath();
    spdlog::info("Added sync path: {}",
                 std::filesystem::absolute(syncPath).string());
    return 0;
  }

  case Command::SyncRun: {
    const std::filesystem::path syncPath = argv[3];
    FileScanner fileScanner(syncPath);
    QueueRepo queueRepo(dbInstance);
    SyncRepo syncRepo(dbInstance);
    SyncService syncService(syncRepo, queueRepo, fileScanner);
    const size_t insertedCount = syncService.scanRoot();
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

  case Command::QueueStats:
    {
      QueueRepo queueRepo(dbInstance);
      SyncRepo syncRepo(dbInstance);
      QueueService queueService(queueRepo, syncRepo);
      const QueueStats stats = queueService.stats();
      spdlog::info("Queue stats: queued={}, running={}, failed={}, done={}",
                   stats.queued, stats.running, stats.failed, stats.done);
    }
    return 0;

  case Command::QueueProcess:
    spdlog::info("queue process not implemented yet");
    return 0;

  case Command::QueueRetryFailed:
    {
      QueueRepo queueRepo(dbInstance);
      SyncRepo syncRepo(dbInstance);
      QueueService queueService(queueRepo, syncRepo);
      queueService.retryFailed();
      spdlog::info("Retried failed queue jobs");
    }
    return 0;

  case Command::QueueClearDone:
    {
      QueueRepo queueRepo(dbInstance);
      SyncRepo syncRepo(dbInstance);
      QueueService queueService(queueRepo, syncRepo);
      queueService.clearDone();
      spdlog::info("Cleared done queue jobs");
    }
    return 0;

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
        "arkive-sync queue clear-done | arkive-sync daemon | "
        "arkive-sync upload <path>");
    return 1;
  }

  return 1;
}
