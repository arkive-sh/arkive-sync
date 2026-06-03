#pragma once

#include <optional>
#include <sqlite3.h>
#include <string>

struct AccountRecord {
  std::string baseUrl;
  std::optional<std::string> email;
  std::optional<std::string> vaultSalt;
  std::optional<std::string> encryptedMasterKey;
  std::optional<std::string> vaultSessionKeyId;
};

inline bool hasPersistedVaultMaterial(const AccountRecord &account) {
  return account.email.has_value() && account.vaultSalt.has_value() &&
         account.encryptedMasterKey.has_value();
}

class UserRepo {
public:
  explicit UserRepo(sqlite3 *db);

  std::optional<AccountRecord> getAccount() const;
  void upsertAccount(const AccountRecord &account) const;
  void clearAccount() const;

private:
  sqlite3 *db_;
};
