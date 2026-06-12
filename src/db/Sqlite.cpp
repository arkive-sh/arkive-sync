#include "./Sqlite.hpp"
#include "db/SqliteHelpers.hpp"
#include "platform/AppDataPaths.hpp"
#include <filesystem>
#include <fstream>
#include <array>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace {

constexpr int kBusyTimeoutMs = 5000;

struct MigrationStep {
  int version;
  const char *filename;
};

constexpr std::array<MigrationStep, 7> kMigrations{{
    {1, "001_initial_schema.sql"},
    {2, "002_account_vault_session.sql"},
    {3, "003_entries_local_path_hash.sql"},
    {4, "004_sync_roots_local_path_hash.sql"},
    {5, "005_account_vault_session_blob.sql"},
    {6, "006_upload_resume.sql"},
    {7, "007_entries_remote_metadata.sql"},
}};

std::filesystem::path migrationsDir() {
  return std::filesystem::path(ARKIVE_SYNC_SOURCE_DIR) / "migrations";
}

std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("Failed to open migration file: " + path.string());
  }

  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

int getUserVersion(sqlite3 *db) {
  sqlite3_stmt *stmt = nullptr;
  const int rc =
      sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare user_version query: " +
                             std::string(sqlite3_errmsg(db)));
  }

  StmtUniquePtr ownedStmt(stmt);
  const int stepRc = sqlite3_step(ownedStmt.get());
  if (stepRc != SQLITE_ROW) {
    throw std::runtime_error("Failed to read user_version: " +
                             std::string(sqlite3_errmsg(db)));
  }

  return sqlite3_column_int(ownedStmt.get(), 0);
}

void setUserVersion(sqlite3 *db, int version) {
  const std::string sql =
      "PRAGMA user_version = " + std::to_string(version) + ";";
  execOrThrow(db, sql.c_str());
}

} // namespace

Database::Database() : dbPath_(databasePath()) { initDb(); }

Database::Database(std::filesystem::path dbPath) : dbPath_(std::move(dbPath)) {
  initDb();
}

Database::~Database() { close(); }

sqlite3 *Database::getDb() { return db; }

void Database::initDb() {
  if (!dbPath_.empty() && dbPath_ != ":memory:") {
    std::filesystem::create_directories(dbPath_.parent_path());
  }

  int rc = sqlite3_open(dbPath_.string().c_str(), &db);
  if (rc != SQLITE_OK) {
    const std::string error_message =
        db != nullptr ? sqlite3_errmsg(db) : "unknown SQLite open failure";
    close();
    throw std::runtime_error("Failed to open database: " + error_message);
  }

  sqlite3_busy_timeout(db, kBusyTimeoutMs);
  execOrThrow(db, "PRAGMA cache_size=-65536;");
  execOrThrow(db, "PRAGMA temp_store=FILE;");
  execOrThrow(db, "PRAGMA journal_mode=WAL;");
  migrateSchema();
  verifySchema();
}

void Database::migrateSchema() {
  int currentVersion = getUserVersion(db);

  for (const auto &migration : kMigrations) {
    if (currentVersion >= migration.version) {
      continue;
    }

    const std::filesystem::path migrationPath =
        migrationsDir() / migration.filename;
    const std::string migrationSql = readTextFile(migrationPath);

    spdlog::info("Applying database migration {} from {}", migration.version,
                 migration.filename);
    execOrThrow(db, "BEGIN IMMEDIATE;");
    try {
      execOrThrow(db, migrationSql.c_str());
      setUserVersion(db, migration.version);
      execOrThrow(db, "COMMIT;");
      currentVersion = migration.version;
    } catch (...) {
      sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      throw;
    }
  }
}

void Database::verifySchema() {
  static constexpr const char *verify_sql =
      "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = "
      "'entries' LIMIT 1;";

  sqlite3_stmt *stmt = nullptr;
  const int rc = sqlite3_prepare_v2(db, verify_sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare schema verification query: " +
                             std::string(sqlite3_errmsg(db)));
  }

  const int step_rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (step_rc != SQLITE_ROW) {
    throw std::runtime_error("Schema verification failed: entries table was "
                             "not found after initialization");
  }

  spdlog::info("Database schema is ready");
}

void Database::close() {
  if (db != nullptr) {
    releaseMemory(db);
    sqlite3_close(db);
    db = nullptr;
  }
}
