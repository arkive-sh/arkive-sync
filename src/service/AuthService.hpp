#pragma once

#include "api/ArkiveClient.hpp"
#include "repo/UserRepo.hpp"

class AuthService {
public:
  AuthService(UserRepo &userRepo, ArkiveClient &client);

  void login(const std::string &email, const std::string &password);
  void logout();

private:
  UserRepo &userRepo_;
  ArkiveClient &client_;
};
