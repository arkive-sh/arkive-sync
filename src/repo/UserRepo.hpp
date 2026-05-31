#pragma once

#include <optional>
#include <sqlite3.h>
#include <string>

struct AccountRecord {
  std::string baseUrl;
  std::optional<std::string> email;
  std::optional<std::string> vaultSalt;
  std::optional<std::string> encryptedMasterKey;
};

class UserRepo {
public:
  explicit UserRepo(sqlite3 *db);

  std::optional<AccountRecord> getAccount() const;
  void upsertAccount(const AccountRecord &account) const;
  void clearAccount() const;

private:
  sqlite3 *db_;
};
