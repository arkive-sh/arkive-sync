#pragma once

#include <cstdint>
#include <string>
#include <vector>

class RustCrypto;
class VaultService;

class LocalPathProtector {
public:
  LocalPathProtector(RustCrypto &crypto, VaultService &vaultService);

  std::string hashPath(const std::string &portablePath);
  std::string encryptPath(const std::string &syncRootId,
                          const std::string &portablePath);
  std::string decryptPath(const std::string &syncRootId,
                          const std::string &encryptedPath,
                          const std::string &pathHash);

private:
  std::vector<uint8_t> deriveEncryptionKey();
  std::vector<uint8_t> deriveLookupKey();
  void ensureUnlocked();

  RustCrypto &crypto_;
  VaultService &vaultService_;
};
