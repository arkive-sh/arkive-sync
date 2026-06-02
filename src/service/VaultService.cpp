#include "service/VaultService.hpp"
#include "crypto/Aad.hpp"
#include <cctype>
#include <stdexcept>

namespace {

int decodeBase64Char(char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A';
  }
  if (ch >= 'a' && ch <= 'z') {
    return ch - 'a' + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return ch - '0' + 52;
  }
  if (ch == '+') {
    return 62;
  }
  if (ch == '/') {
    return 63;
  }
  return -1;
}

std::vector<uint8_t> decodeBase64(const std::string &input) {
  std::vector<uint8_t> output;
  int val = 0;
  int valb = -8;

  for (unsigned char rawCh : input) {
    const char ch = static_cast<char>(rawCh);
    if (std::isspace(rawCh)) {
      continue;
    }
    if (ch == '=') {
      break;
    }

    const int decoded = decodeBase64Char(ch);
    if (decoded < 0) {
      throw std::invalid_argument("Invalid base64 input");
    }

    val = (val << 6) + decoded;
    valb += 6;
    if (valb >= 0) {
      output.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }

  return output;
}

} // namespace

VaultService::VaultService(UserRepo &userRepo, RustCrypto &crypto)
    : userRepo_(userRepo), crypto_(crypto) {}

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
