#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "db/SqliteHelpers.hpp"
#include "helpers/Base64.hpp"
#include "repo/UserRepo.hpp"
#include "service/VaultService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <sqlite3.h>
#include <unordered_map>

namespace {

struct SharedSecrets {
  std::unordered_map<std::string, std::vector<uint8_t>> values;
};

class SharedMemorySecureStorage : public SecureStorage {
public:
  explicit SharedMemorySecureStorage(std::shared_ptr<SharedSecrets> secrets)
      : secrets_(std::move(secrets)) {}

  void storeSecret(const std::string &service, const std::string &account,
                   const std::vector<uint8_t> &secret) override {
    secrets_->values[service + "\n" + account] = secret;
  }

  std::optional<std::vector<uint8_t>>
  loadSecret(const std::string &service,
             const std::string &account) override {
    const auto it = secrets_->values.find(service + "\n" + account);
    if (it == secrets_->values.end()) {
      return std::nullopt;
    }

    return it->second;
  }

  void deleteSecret(const std::string &service,
                    const std::string &account) override {
    secrets_->values.erase(service + "\n" + account);
  }

private:
  std::shared_ptr<SharedSecrets> secrets_;
};

class TestDb {
public:
  TestDb() {
    REQUIRE(sqlite3_open(":memory:", &db_) == SQLITE_OK);
    execOrThrow(db_, R"sql(
CREATE TABLE account (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  base_url TEXT NOT NULL,
  email TEXT,
  vault_salt TEXT,
  encrypted_master_key TEXT,
  vault_session_key_id TEXT,
  vault_session_blob TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO account (id, base_url) VALUES (1, 'http://localhost:8080');
)sql");
  }

  ~TestDb() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

  sqlite3 *get() const { return db_; }

private:
  sqlite3 *db_ = nullptr;
};

void seedAccount(UserRepo &userRepo, RustCrypto &crypto) {
  const std::string password = "test-password";
  const std::vector<uint8_t> salt = crypto.generateSalt();
  const std::vector<uint8_t> masterKey = crypto.generateMasterKey();
  const std::vector<uint8_t> encryptedMasterKey = crypto.wrapMasterKey(
      masterKey, crypto.derivePasswordKek(password, salt),
      ArkiveAad::toBytes(ArkiveAad::kMasterKey));

  userRepo.upsertAccount(AccountRecord{
      .baseUrl = "http://localhost:8080",
      .email = std::string("test@example.com"),
      .vaultSalt = encodeBase64(salt),
      .encryptedMasterKey = encodeBase64(encryptedMasterKey),
      .vaultSessionKeyId = std::nullopt,
      .vaultSessionBlob = std::nullopt,
  });
}

} // namespace

TEST_CASE("VaultService unlock stores session") {
  TestDb db;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  auto sharedSecrets = std::make_shared<SharedSecrets>();
  VaultService vaultService(
      userRepo, crypto,
      std::make_unique<SharedMemorySecureStorage>(sharedSecrets));
  seedAccount(userRepo, crypto);

  vaultService.unlock("test-password");

  const auto account = userRepo.getAccount();
  REQUIRE(account.has_value());
  REQUIRE(account->vaultSessionKeyId.has_value());
  REQUIRE(account->vaultSessionBlob.has_value());
  REQUIRE(!account->vaultSessionKeyId->empty());
  REQUIRE(!account->vaultSessionBlob->empty());
  REQUIRE(sharedSecrets->values.contains("arkive-sync.vault-session\n" +
                                         *account->vaultSessionKeyId));
}

TEST_CASE("VaultService restoreSession restores after new instance") {
  TestDb db;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  auto sharedSecrets = std::make_shared<SharedSecrets>();
  std::vector<uint8_t> originalMasterKey;

  {
    VaultService vaultService(
        userRepo, crypto,
        std::make_unique<SharedMemorySecureStorage>(sharedSecrets));
    seedAccount(userRepo, crypto);
    vaultService.unlock("test-password");
    originalMasterKey = vaultService.masterKey();
    vaultService.lock();
  }

  VaultService restoredService(
      userRepo, crypto,
      std::make_unique<SharedMemorySecureStorage>(sharedSecrets));

  REQUIRE(restoredService.restoreSession());
  REQUIRE(restoredService.isUnlocked());
  REQUIRE(restoredService.masterKey() == originalMasterKey);
}

TEST_CASE("VaultService lock clears memory") {
  TestDb db;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  auto sharedSecrets = std::make_shared<SharedSecrets>();
  VaultService vaultService(
      userRepo, crypto,
      std::make_unique<SharedMemorySecureStorage>(sharedSecrets));
  seedAccount(userRepo, crypto);

  vaultService.unlock("test-password");
  REQUIRE(vaultService.isUnlocked());

  vaultService.lock();

  REQUIRE_FALSE(vaultService.isUnlocked());
  REQUIRE_THROWS(vaultService.masterKey());
}

TEST_CASE("VaultService clearPersistedSession removes keychain entry") {
  TestDb db;
  RustCrypto crypto;
  UserRepo userRepo(db.get());
  auto sharedSecrets = std::make_shared<SharedSecrets>();
  VaultService vaultService(
      userRepo, crypto,
      std::make_unique<SharedMemorySecureStorage>(sharedSecrets));
  seedAccount(userRepo, crypto);
  vaultService.unlock("test-password");

  const auto accountBefore = userRepo.getAccount();
  REQUIRE(accountBefore.has_value());
  REQUIRE(accountBefore->vaultSessionKeyId.has_value());

  vaultService.clearPersistedSession();

  const auto accountAfter = userRepo.getAccount();
  REQUIRE(accountAfter.has_value());
  REQUIRE_FALSE(accountAfter->vaultSessionKeyId.has_value());
  REQUIRE_FALSE(accountAfter->vaultSessionBlob.has_value());
  REQUIRE(sharedSecrets->values.empty());
}
