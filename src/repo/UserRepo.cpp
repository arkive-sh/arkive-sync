#include "repo/UserRepo.hpp"
#include "db/SqliteHelpers.hpp"

#include <sqlite3.h>
#include <stdexcept>

UserRepo::UserRepo(sqlite3 *db) : db_(db) {
  if (db == nullptr) {
    throw std::invalid_argument("User Repo needs a valid sqlite3 connection");
  }
}

std::optional<AccountRecord> UserRepo::getAccount() const {
  static constexpr const char *getAccountSql = R"sql(
SELECT
base_url,
email,
vault_salt,
encrypted_master_key
FROM account
WHERE id = 1;
  )sql";

  sqlite3_stmt *raw_stmt = nullptr;
  if (sqlite3_prepare_v2(db_, getAccountSql, -1, &raw_stmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(raw_stmt);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }

  const char *baseUrl =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 0));
  const char *email =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 1));
  const char *vaultSalt =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 2));
  const char *encryptedMasterKey =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt.get(), 3));

  if (baseUrl == nullptr) {
    throw std::invalid_argument("account.base_url was NULL");
  }

  return AccountRecord{
      .baseUrl = baseUrl,
      .email =
          email != nullptr ? std::optional<std::string>(email) : std::nullopt,
      .vaultSalt = vaultSalt != nullptr ? std::optional<std::string>(vaultSalt)
                                        : std::nullopt,
      .encryptedMasterKey = encryptedMasterKey != nullptr
                                ? std::optional<std::string>(encryptedMasterKey)
                                : std::nullopt,
  };
}

void UserRepo::upsertAccount(const AccountRecord &account) const {
  static constexpr const char *upsertAccountSql = R"sql(
INSERT INTO account (
  id,
  base_url,
  email,
  vault_salt,
  encrypted_master_key,
  created_at,
  updated_at
) VALUES (
  1,
  ?,
  ?,
  ?,
  ?,
  CURRENT_TIMESTAMP,
  CURRENT_TIMESTAMP
)
ON CONFLICT(id) DO UPDATE SET
  base_url = excluded.base_url,
  email = excluded.email,
  vault_salt = excluded.vault_salt,
  encrypted_master_key = excluded.encrypted_master_key,
  updated_at = CURRENT_TIMESTAMP;
  )sql";

  sqlite3_stmt *raw_stmt = nullptr;
  if (sqlite3_prepare_v2(db_, upsertAccountSql, -1, &raw_stmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(raw_stmt);

  bindText(db_, stmt.get(), 1, account.baseUrl);
  bindOptionalText(db_, stmt.get(), 2, account.email);
  bindOptionalText(db_, stmt.get(), 3, account.vaultSalt);
  bindOptionalText(db_, stmt.get(), 4, account.encryptedMasterKey);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}

void UserRepo::clearAccount() const {
  static constexpr const char *clearAccountSql = R"sql(
UPDATE account
SET
  email = NULL,
  vault_salt = NULL,
  encrypted_master_key = NULL,
  updated_at = CURRENT_TIMESTAMP
WHERE id = 1;
  )sql";

  sqlite3_stmt *raw_stmt = nullptr;
  if (sqlite3_prepare_v2(db_, clearAccountSql, -1, &raw_stmt, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(std::string("Prepare failed: ") +
                             sqlite3_errmsg(db_));
  }

  StmtUniquePtr stmt(raw_stmt);

  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("Step failed: ") +
                             sqlite3_errmsg(db_));
  }
}
