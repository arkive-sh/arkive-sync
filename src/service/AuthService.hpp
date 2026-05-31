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

  bool login(const std::string &email, const std::string &password);
  bool logout();
  nlohmann::json me();

private:
  bool hasValidSession();

  UserRepo &userRepo_;
  ArkiveHttpClient &client_;
};
