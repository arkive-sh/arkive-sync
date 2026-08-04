#include "ipc/commands/Commands.hpp"

#include "platform/DaemonServices.hpp"
#include "repo/UserRepo.hpp"
#include "service/AuthService.hpp"
#include "service/VaultService.hpp"

#include <stdexcept>

namespace ipc::commands {

arkive::ipc::Response login(DaemonServices &services,
                            const arkive::ipc::Request &request) {
  if (services.authService == nullptr) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }
  const auto account = services.userRepo->getAccount();
  if (!account.has_value()) {
    throw std::runtime_error("Base URL is missing");
  }

  if (services.authService->hasValidSession() && request.email().empty()) {
    if (!hasPersistedVaultMaterial(*account)) {
      services.authService->refreshVaultMaterial(request.password());
    }
    services.vaultService->unlock(request.password());
  } else {
    services.authService->login(request.email(), request.password());
    services.vaultService->unlock(request.password());
  }

  arkive::ipc::Response response;
  response.set_ok(true);
  response.set_state("logged_in");
  response.set_vault_unlocked(services.vaultService->isUnlocked());
  return response;
}

arkive::ipc::Response logout(DaemonServices &services) {
  if (services.authService == nullptr) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }
  services.vaultService->lock();
  services.vaultService->clearPersistedSession();
  services.authService->logout();

  arkive::ipc::Response response;
  response.set_ok(true);
  response.set_state("logged_out");
  return response;
}

} // namespace ipc::commands
