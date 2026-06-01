#include "service/AuthService.hpp"
#include "./helpers/Helpers.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

bool isUnauthorized(const HttpError &error) {
  return error.statusCode() == 401 || error.statusCode() == 403;
}

bool hasPersistedAccountData(const AccountRecord &account) {
  return account.email.has_value() && account.vaultSalt.has_value() &&
         account.encryptedMasterKey.has_value();
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

bool AuthService::login() {
  const auto account = userRepo_.getAccount();
  if (!account.has_value()) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }

  try {
    ensureValidSession();
    if (hasPersistedAccountData(*account)) {
      return false;
    }
  } catch (const std::runtime_error &error) {
    if (std::string(error.what()) !=
        "No valid session. Run: arkive-sync login") {
      throw;
    }
  }

  std::string email, password;

  // Get email input
  std::cout << "Enter your email: ";
  std::getline(std::cin, email);

  if (email == "") {
    throw std::invalid_argument("Email cannot be empty");
  }

  // hide password from terminal
  std::cout << "Enter your password:" << std::flush;
  // extra scope so terminal goes back to normal regardless of what happens RAII
  {
    auto echoGuard = makeTerminalEchoGuard();
    std::getline(std::cin, password);
  }
  std::cout << '\n';

  if (password == "") {
    throw std::invalid_argument("Password cannot be empty!");
  }

  const LoginResponse response = api_.login(email, password);

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
