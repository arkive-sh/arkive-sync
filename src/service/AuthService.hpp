#pragma once

#include "api/ArkiveApi.hpp"
#include "repo/UserRepo.hpp"

class AuthService {
public:
  AuthService(UserRepo &userRepo, ArkiveApi &api);

  bool login(const std::string &email, const std::string &password);
  void refreshVaultMaterial(const std::string &password);
  bool logout();
  nlohmann::json me();
  void ensureValidSession();
  bool hasValidSession();

private:
  UserRepo &userRepo_;
  ArkiveApi &api_;
};
