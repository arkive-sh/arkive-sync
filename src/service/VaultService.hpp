#pragma once

#include "crypto/RustCrypto.hpp"
#include "platform/SecureStorage.hpp"
#include "repo/UserRepo.hpp"
#include <memory>
#include <string>
#include <vector>

class VaultService {
public:
  VaultService(UserRepo &userRepo, RustCrypto &crypto);
  VaultService(UserRepo &userRepo, RustCrypto &crypto,
               std::unique_ptr<SecureStorage> secureStorage);
  ~VaultService();

  VaultService(const VaultService &) = delete;
  VaultService &operator=(const VaultService &) = delete;

  void unlock(const std::string &password);
  bool restoreSession();
  void clearPersistedSession();
  void lock();
  bool isUnlocked() const noexcept;
  const std::vector<uint8_t> &masterKey() const;

private:
  void persistSession();

  UserRepo &userRepo_;
  RustCrypto &crypto_;
  std::unique_ptr<SecureStorage> secureStorage_;
  std::vector<uint8_t> masterKey_;
};
