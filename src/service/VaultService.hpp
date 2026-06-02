#pragma once

#include "crypto/RustCrypto.hpp"
#include "repo/UserRepo.hpp"
#include <string>
#include <vector>

class VaultService {
public:
  explicit VaultService(UserRepo &userRepo);
  ~VaultService();

  VaultService(const VaultService &) = delete;
  VaultService &operator=(const VaultService &) = delete;

  void unlock(const std::string &password);
  void lock();
  bool isUnlocked() const noexcept;
  const std::vector<uint8_t> &masterKey() const;

private:
  UserRepo &userRepo_;
  RustCrypto crypto_;
  std::vector<uint8_t> masterKey_;
};
