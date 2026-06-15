#include "service/AuthService.hpp"
#include <stdexcept>
#include <string>

namespace {

bool isUnauthorized(const HttpError &error) {
  return error.statusCode == 401 || error.statusCode == 403;
}

void applyIdentity(AccountRecord &account, const nlohmann::json &me) {
  const std::string userId = me.value("id", "");
  if (userId.empty()) {
    throw std::runtime_error("Could not load user id from /api/me");
  }

  account.userId = userId;
}

} // namespace

void AuthService::ensureValidSession() {
  try {
    api_.me();
  } catch (const HttpError &error) {
    if (isUnauthorized(error)) {
      throw std::runtime_error("No valid session. Run: arkive-sync login");
    }

    throw;
  }
}

AuthService::AuthService(UserRepo &userRepo, ArkiveApi &api)
    : userRepo_(userRepo), api_(api) {}

bool AuthService::hasValidSession() {
  try {
    ensureValidSession();
    return true;
  } catch (const std::runtime_error &error) {
    if (std::string(error.what()) ==
        "No valid session. Run: arkive-sync login") {
      return false;
    }
    throw;
  }
}

bool AuthService::login(const std::string &email, const std::string &password) {
  const auto account = userRepo_.getAccount();
  if (!account.has_value()) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }

  if (email.empty()) {
    throw std::invalid_argument("Email cannot be empty");
  }
  if (password.empty()) {
    throw std::invalid_argument("Password cannot be empty!");
  }

  const LoginResponse response = api_.login(email, password);
  const nlohmann::json me = api_.me();

  AccountRecord updated{
      .baseUrl = account->baseUrl,
      .userId = account->userId,
      .email = email,
      .vaultSalt = response.salt.empty()
                       ? std::nullopt
                       : std::optional<std::string>(response.salt),
      .encryptedMasterKey =
          response.encryptedMasterKey.empty()
              ? std::nullopt
              : std::optional<std::string>(response.encryptedMasterKey),
      .vaultSessionKeyId = account->vaultSessionKeyId,
      .vaultSessionBlob = account->vaultSessionBlob,
  };
  applyIdentity(updated, me);
  userRepo_.upsertAccount(updated);

  return true;
}

void AuthService::refreshVaultMaterial(const std::string &password) {
  const auto account = userRepo_.getAccount();
  if (!account.has_value()) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }
  if (password.empty()) {
    throw std::invalid_argument("Password cannot be empty!");
  }

  const LoginResponse response = api_.unlockVault(password);
  const nlohmann::json me = api_.me();
  AccountRecord updated{
      .baseUrl = account->baseUrl,
      .userId = account->userId,
      .email = account->email,
      .vaultSalt = response.salt.empty()
                       ? std::nullopt
                       : std::optional<std::string>(response.salt),
      .encryptedMasterKey =
          response.encryptedMasterKey.empty()
              ? std::nullopt
              : std::optional<std::string>(response.encryptedMasterKey),
      .vaultSessionKeyId = account->vaultSessionKeyId,
      .vaultSessionBlob = account->vaultSessionBlob,
  };
  applyIdentity(updated, me);
  userRepo_.upsertAccount(updated);
}

bool AuthService::logout() {
  try {
    ensureValidSession();
  } catch (const std::runtime_error &error) {
    if (std::string(error.what()) !=
        "No valid session. Run: arkive-sync login") {
      throw;
    }

    userRepo_.clearAccount();
    return false;
  }

  api_.logout();
  userRepo_.clearAccount();
  return true;
}

nlohmann::json AuthService::me() { return api_.me(); }
