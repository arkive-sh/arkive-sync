#include "App.hpp"
#include "./api/ArkiveClient.hpp"
#include "./db/Sqlite.hpp"
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <string>

namespace {

enum class Command {
  Login,
  Status,
  Upload,
  Unknown,
};

Command parseCommand(const std::string &command) {
  if (command == "login") {
    return Command::Login;
  }

  if (command == "status") {
    return Command::Status;
  }

  if (command == "upload") {
    return Command::Upload;
  }

  return Command::Unknown;
}

} // namespace

App::App() {}

App::~App() {}

int App::run(int argc, char *argv[]) {
  Database db;
  sqlite3 *dbInstance = db.getDb();
  if (argc < 2) {
    spdlog::info("Usage: arkive-sync <status|login|upload|download>");
    return 0;
  }

  const std::string command = argv[1];

  switch (parseCommand(command)) {
  case Command::Login: {
    // call arkive api client for login
    spdlog::info("Logging into arkive");

    ArkiveClient client("http://localhost:8080",
                        "/home/archnuman/.local/share/arkive-sync/cookies.txt");

    auto res = client.login("numan@gmail.com", "12345678@Aa");
    auto me = client.me();
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

  case Command::Unknown:
    spdlog::error("Unknown command: {}", command);
    return 1;
  }

  return 1;
}
