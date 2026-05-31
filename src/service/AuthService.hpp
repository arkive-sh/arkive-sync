#pragma once

#include "api/ArkiveHttpClient.hpp"
#include "repo/UserRepo.hpp"

struct LoginResponse {
  std::string salt;
  std::string encryptedMasterKey;
};

class AuthService {
public:
  AuthService(UserRepo &userRepo, ArkiveHttpClient &client);

  bool login();
  bool logout();
  nlohmann::json me();
  void ensureValidSession();

private:
  UserRepo &userRepo_;
  ArkiveHttpClient &client_;
};
