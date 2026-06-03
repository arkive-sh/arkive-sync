#include "service/VaultService.hpp"
#include "crypto/Aad.hpp"
#include "helpers/Base64.hpp"
#include <stdexcept>

namespace {

constexpr char kVaultSessionService[] = "arkive-sync.vault-session";

std::string makeVaultSessionKeyId(RustCrypto &crypto, const AccountRecord &account) {
  if (!account.email.has_value() || account.email->empty()) {
    throw std::runtime_error(
        "Vault session cannot be persisted without an account email");
  }

  const std::string scope = account.baseUrl + "\n" + *account.email;
  return crypto.sha256HashHex(std::vector<uint8_t>(scope.begin(), scope.end()));
}

} // namespace

VaultService::VaultService(UserRepo &userRepo, RustCrypto &crypto)
    : userRepo_(userRepo), crypto_(crypto),
      secureStorage_(SecureStorage::create()) {}

VaultService::~VaultService() { lock(); }

void VaultService::unlock(const std::string &password) {
  const auto account = userRepo_.getAccount();
  if (!account.has_value() || !account->vaultSalt.has_value() ||
      !account->encryptedMasterKey.has_value()) {
    throw std::runtime_error(
        "Vault material is unavailable. Login or unlock with Arkive first.");
  }

  std::vector<uint8_t> salt = decodeBase64(*account->vaultSalt);
  std::vector<uint8_t> encryptedMasterKey =
      decodeBase64(*account->encryptedMasterKey);
  std::vector<uint8_t> aad = ArkiveAad::toBytes(ArkiveAad::kMasterKey);
  std::vector<uint8_t> kek;
  std::vector<uint8_t> unwrappedMasterKey;

  try {
    kek = crypto_.derivePasswordKek(password, salt);
    unwrappedMasterKey = crypto_.unwrapMasterKey(encryptedMasterKey, kek, aad);

    lock();
    masterKey_ = unwrappedMasterKey;
    persistSession();
  } catch (...) {
    if (!kek.empty()) {
      crypto_.zeroize(kek);
    }
    if (!unwrappedMasterKey.empty()) {
      crypto_.zeroize(unwrappedMasterKey);
    }
    if (!salt.empty()) {
      crypto_.zeroize(salt);
    }
    if (!encryptedMasterKey.empty()) {
      crypto_.zeroize(encryptedMasterKey);
    }
    throw;
  }

  if (!kek.empty()) {
    crypto_.zeroize(kek);
  }
  if (!unwrappedMasterKey.empty()) {
    crypto_.zeroize(unwrappedMasterKey);
  }
  if (!salt.empty()) {
    crypto_.zeroize(salt);
  }
  if (!encryptedMasterKey.empty()) {
    crypto_.zeroize(encryptedMasterKey);
  }
}

bool VaultService::restoreSession() {
  if (isUnlocked()) {
    return true;
  }

  const auto account = userRepo_.getAccount();
  if (!account.has_value() || !account->vaultSessionKeyId.has_value() ||
      account->vaultSessionKeyId->empty()) {
    return false;
  }

  const auto restored =
      secureStorage_->loadSecret(kVaultSessionService, *account->vaultSessionKeyId);
  if (!restored.has_value() || restored->empty()) {
    AccountRecord updated = *account;
    updated.vaultSessionKeyId = std::nullopt;
    userRepo_.upsertAccount(updated);
    return false;
  }

  lock();
  masterKey_ = *restored;
  return true;
}

void VaultService::clearPersistedSession() {
  const auto account = userRepo_.getAccount();
  if (!account.has_value() || !account->vaultSessionKeyId.has_value() ||
      account->vaultSessionKeyId->empty()) {
    return;
  }

  secureStorage_->deleteSecret(kVaultSessionService, *account->vaultSessionKeyId);
}

void VaultService::lock() {
  if (!masterKey_.empty()) {
    crypto_.zeroize(masterKey_);
    masterKey_.clear();
  }
}

bool VaultService::isUnlocked() const noexcept { return !masterKey_.empty(); }

const std::vector<uint8_t> &VaultService::masterKey() const {
  if (masterKey_.empty()) {
    throw std::runtime_error("Vault is locked");
  }

  return masterKey_;
}

void VaultService::persistSession() {
  if (masterKey_.empty()) {
    throw std::runtime_error("Cannot persist a locked vault session");
  }

  const auto account = userRepo_.getAccount();
  if (!account.has_value()) {
    throw std::runtime_error("Cannot persist a vault session without an account");
  }

  const std::string sessionKeyId = makeVaultSessionKeyId(crypto_, *account);

  if (account->vaultSessionKeyId.has_value() &&
      *account->vaultSessionKeyId != sessionKeyId) {
    secureStorage_->deleteSecret(kVaultSessionService, *account->vaultSessionKeyId);
  }

  secureStorage_->storeSecret(kVaultSessionService, sessionKeyId, masterKey_);

  AccountRecord updated = *account;
  updated.vaultSessionKeyId = sessionKeyId;
  userRepo_.upsertAccount(updated);
}
