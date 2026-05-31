#include "service/AuthService.hpp"

bool AuthService::hasValidSession() {
  try {
    client_.getJson("/api/me");
    return true;
  } catch (const HttpError &error) {
    if (error.statusCode() == 401 || error.statusCode() == 403) {
      return false;
    }

    throw;
  }
}

AuthService::AuthService(UserRepo &userRepo, ArkiveHttpClient &client)
    : userRepo_(userRepo), client_(client) {}

bool AuthService::login(const std::string &email, const std::string &password) {
  const auto account = userRepo_.getAccount();
  if (!account.has_value()) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }

  if (hasValidSession()) {
    return false;
  }

  const auto responseJson =
      client_.postJson("/api/auth/login", {{"email", email}, {"password", password}});
  const LoginResponse response{
      .salt = responseJson.value("salt", ""),
      .encryptedMasterKey = responseJson.value("encryptedMasterKey", ""),
  };

  userRepo_.upsertAccount(AccountRecord{
      .baseUrl = account->baseUrl,
      .email = email,
      .vaultSalt = response.salt.empty()
                       ? std::nullopt
                       : std::optional<std::string>(response.salt),
      .encryptedMasterKey =
          response.encryptedMasterKey.empty()
              ? std::nullopt
              : std::optional<std::string>(response.encryptedMasterKey),
  });

  return true;
}

bool AuthService::logout() {
  if (!hasValidSession()) {
    userRepo_.clearAccount();
    return false;
  }

  client_.postForm("/logout");
  userRepo_.clearAccount();
  return true;
}

nlohmann::json AuthService::me() { return client_.getJson("/api/me"); }
