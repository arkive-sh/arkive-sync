#include "ipc/commands/Commands.hpp"

#include "platform/DaemonServices.hpp"
#include "repo/UserRepo.hpp"
#include "service/AuthService.hpp"
#include "service/SyncService.hpp"
#include "service/VaultService.hpp"

namespace ipc::commands {

arkive::ipc::Response status(DaemonServices &services) {
  arkive::ipc::Response response;
  response.set_ok(true);
  response.set_state("running");
  response.set_sync_root_count(
      static_cast<uint32_t>(services.syncService->getSyncRoots().size()));
  if (services.authService != nullptr) {
    response.set_authenticated(services.authService->hasValidSession());
  }
  response.set_vault_unlocked(services.vaultService->isUnlocked());
  if (const auto account = services.userRepo->getAccount();
      account.has_value() && account->email.has_value()) {
    response.set_email(*account->email);
  }
  return response;
}

} // namespace ipc::commands
