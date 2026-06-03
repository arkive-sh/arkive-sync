#include "helpers/LocalPathProtector.hpp"

#include "crypto/Aad.hpp"
#include "crypto/RustCrypto.hpp"
#include "helpers/Base64.hpp"
#include "helpers/Hex.hpp"
#include "service/VaultService.hpp"
#include <stdexcept>
#include <vector>

namespace {

std::vector<uint8_t> makePathAad(const std::string &syncRootId,
                                 const std::string &pathHash) {
  return ArkiveAad::toBytes(ArkiveAad::makeLocalPath(syncRootId, pathHash));
}

} // namespace

LocalPathProtector::LocalPathProtector(RustCrypto &crypto,
                                       VaultService &vaultService)
    : crypto_(crypto), vaultService_(vaultService) {}

std::string LocalPathProtector::hashPath(const std::string &portablePath) {
  ensureUnlocked();

  std::vector<uint8_t> lookupKey = deriveLookupKey();
  std::vector<uint8_t> digest;
  try {
    digest = crypto_.hmacSha256(lookupKey, ArkiveAad::toBytes(portablePath));
    const std::string encoded = encodeHex(digest);
    crypto_.zeroize(digest);
    crypto_.zeroize(lookupKey);
    return encoded;
  } catch (...) {
    if (!digest.empty()) {
      crypto_.zeroize(digest);
    }
    if (!lookupKey.empty()) {
      crypto_.zeroize(lookupKey);
    }
    throw;
  }
}

std::string LocalPathProtector::encryptPath(const std::string &syncRootId,
                                            const std::string &portablePath) {
  ensureUnlocked();

  std::vector<uint8_t> encryptionKey = deriveEncryptionKey();
  std::vector<uint8_t> plaintext(portablePath.begin(), portablePath.end());
  std::vector<uint8_t> ciphertext;

  try {
    const std::string pathHash = hashPath(portablePath);
    const std::vector<uint8_t> aad = makePathAad(syncRootId, pathHash);
    ciphertext = crypto_.encryptChunk(encryptionKey, aad, plaintext);
    const std::string encoded = encodeBase64(ciphertext);
    crypto_.zeroize(ciphertext);
    crypto_.zeroize(plaintext);
    crypto_.zeroize(encryptionKey);
    return encoded;
  } catch (...) {
    if (!ciphertext.empty()) {
      crypto_.zeroize(ciphertext);
    }
    if (!plaintext.empty()) {
      crypto_.zeroize(plaintext);
    }
    if (!encryptionKey.empty()) {
      crypto_.zeroize(encryptionKey);
    }
    throw;
  }
}

std::string LocalPathProtector::decryptPath(const std::string &syncRootId,
                                            const std::string &encryptedPath,
                                            const std::string &pathHash) {
  ensureUnlocked();

  std::vector<uint8_t> encryptionKey = deriveEncryptionKey();
  std::vector<uint8_t> ciphertext = decodeBase64(encryptedPath);
  std::vector<uint8_t> plaintext;

  try {
    const std::vector<uint8_t> aad = makePathAad(syncRootId, pathHash);
    plaintext = crypto_.decryptChunk(encryptionKey, aad, ciphertext);
    const std::string decoded(plaintext.begin(), plaintext.end());
    crypto_.zeroize(plaintext);
    crypto_.zeroize(ciphertext);
    crypto_.zeroize(encryptionKey);
    return decoded;
  } catch (...) {
    if (!plaintext.empty()) {
      crypto_.zeroize(plaintext);
    }
    if (!ciphertext.empty()) {
      crypto_.zeroize(ciphertext);
    }
    if (!encryptionKey.empty()) {
      crypto_.zeroize(encryptionKey);
    }
    throw;
  }
}

std::vector<uint8_t> LocalPathProtector::deriveEncryptionKey() {
  return crypto_.hmacSha256(vaultService_.masterKey(),
                            ArkiveAad::toBytes(ArkiveAad::kLocalPathKey));
}

std::vector<uint8_t> LocalPathProtector::deriveLookupKey() {
  return crypto_.hmacSha256(vaultService_.masterKey(),
                            ArkiveAad::toBytes(ArkiveAad::kLocalPathHashKey));
}

void LocalPathProtector::ensureUnlocked() {
  if (!vaultService_.isUnlocked()) {
    vaultService_.restoreSession();
  }
  if (!vaultService_.isUnlocked()) {
    throw std::runtime_error(
        "Vault is locked. Run `arkive-sync login` to unlock or restore the vault session.");
  }
}
