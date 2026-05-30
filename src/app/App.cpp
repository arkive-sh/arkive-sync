#include "App.hpp"
#include "./db/sqlite.hpp"
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <string>

App::App() { spdlog::info("App created"); }

App::~App() { spdlog::info("App shutting down"); }

int App::run(int argc, char *argv[]) {
  Database db;
  sqlite3 *dbInstance = db.getDb();
  if (argc < 2) {
    spdlog::info("Usage: arkive-sync <status|login|upload|download>");
    return 0;
  }

  std::string command = argv[1];

  if (command == "login") {
    // call arkive api client for login
    spdlog::info("Logging into arkive");
  }

  if (command == "status") {
    spdlog::info("Arkive Sync is installed and working");
    return 0;
  }

  if (command == "upload") {
    if (argc < 3) {
      spdlog::error("Usage: arkive-sync upload <path>");
      return 1;
    }

    std::string path = argv[2];
    spdlog::info("Upload requested for: {}", path);
    return 0;
  }

  spdlog::error("Unknown command: {}", command);
  return 1;
}
