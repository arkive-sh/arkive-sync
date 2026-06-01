#include "./Sqlite.hpp"
#include "db/SqliteHelpers.hpp"
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

Database::Database() { initDb(); }

Database::~Database() { close(); }

sqlite3 *Database::getDb() { return db; }

void Database::initDb() {
  int rc = sqlite3_open("arkive-sync.db", &db);
  if (rc != SQLITE_OK) {
    const std::string error_message =
        db != nullptr ? sqlite3_errmsg(db) : "unknown SQLite open failure";
    close();
    throw std::runtime_error("Failed to open database: " + error_message);
  }

  createSchema();
  verifySchema();
}

void Database::createSchema() {
  static constexpr const char *schema_sql = R"sql(
    CREATE TABLE IF NOT EXISTS settings (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );

    CREATE TABLE IF NOT EXISTS account (
      id INTEGER PRIMARY KEY CHECK (id = 1),
      base_url TEXT NOT NULL DEFAULT 'http://localhost:8080',
      email TEXT,
      vault_salt TEXT,
      encrypted_master_key TEXT,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );

    CREATE TABLE IF NOT EXISTS sync_roots (
      id TEXT PRIMARY KEY,
      local_path TEXT NOT NULL UNIQUE,
      folder_id TEXT,
      enabled INTEGER NOT NULL DEFAULT 1,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );

    CREATE TABLE IF NOT EXISTS entries (
      id TEXT PRIMARY KEY,
      remote_id TEXT,
      sync_root_id TEXT NOT NULL,
      remote_type TEXT NOT NULL,
      local_path TEXT NOT NULL,
      is_directory INTEGER NOT NULL DEFAULT 0,
      parent_folder_id TEXT,
      encrypted_name TEXT,
      local_size INTEGER,
      local_mtime TEXT,
      local_hash TEXT,
      remote_updated_at TEXT,
      sync_state TEXT NOT NULL,
      last_synced_at TEXT,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );

    CREATE TABLE IF NOT EXISTS transfer_queue (
      id TEXT PRIMARY KEY,
      entry_id TEXT,
      direction TEXT NOT NULL,
      status TEXT NOT NULL,
      local_path TEXT NOT NULL,
      remote_id TEXT,
      folder_id TEXT,
      bytes_total INTEGER,
      bytes_done INTEGER NOT NULL DEFAULT 0,
      error_message TEXT,
      retry_count INTEGER NOT NULL DEFAULT 0,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );

    CREATE INDEX IF NOT EXISTS idx_transfer_queue_status ON transfer_queue(status);
  )sql";

  char *error_message = nullptr;
  const int rc = sqlite3_exec(db, schema_sql, nullptr, nullptr, &error_message);
  if (rc != SQLITE_OK) {
    const std::string message =
        error_message != nullptr ? error_message : "unknown schema error";
    sqlite3_free(error_message);
    throw std::runtime_error("Failed to create schema: " + message);
  }

  execOrThrow(
      db,
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_entries_sync_root_local_path "
      "ON entries(sync_root_id, local_path);");
  execOrThrow(db,
              "INSERT INTO account (id, base_url) VALUES (1, 'http://localhost:8080') "
              "ON CONFLICT(id) DO NOTHING;");
  execOrThrow(db,
              "CREATE INDEX IF NOT EXISTS idx_entries_remote_id ON "
              "entries(remote_id);");
  execOrThrow(db,
              "CREATE INDEX IF NOT EXISTS idx_entries_sync_state ON "
              "entries(sync_state);");
  execOrThrow(db, "DROP INDEX IF EXISTS idx_entries_local_path;");
  execOrThrow(
      db,
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_transfer_active_upload "
      "ON transfer_queue(entry_id, direction) "
      "WHERE direction = 'upload' "
      "AND status IN ('queued', 'running');");
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
    sqlite3_close(db);
    db = nullptr;
  }
}
