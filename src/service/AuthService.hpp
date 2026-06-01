#pragma once

#include "api/ArkiveApi.hpp"
#include "repo/UserRepo.hpp"

class AuthService {
public:
  AuthService(UserRepo &userRepo, ArkiveApi &api);

  bool login();
  bool logout();
  nlohmann::json me();
  void ensureValidSession();

private:
  UserRepo &userRepo_;
  ArkiveApi &api_;
};
