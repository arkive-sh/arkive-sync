#include "service/AuthService.hpp"

AuthService::AuthService(UserRepo &userRepo, ArkiveClient &client)
    : userRepo_(userRepo), client_(client) {}

void AuthService::login(const std::string &email, const std::string &password) {
  const auto account = userRepo_.getAccount();
  if (!account.has_value()) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }

  const LoginResponse response = client_.login(email, password);

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
}

void AuthService::logout() {
  client_.logout();
  userRepo_.clearAccount();
}
