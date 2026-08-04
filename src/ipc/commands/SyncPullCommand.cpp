#include "ipc/commands/Commands.hpp"

#include "platform/DaemonServices.hpp"
#include "service/RemoteSyncService.hpp"
#include "service/SyncReconciler.hpp"
#include "service/SyncService.hpp"

#include <stdexcept>

namespace ipc::commands {

arkive::ipc::Response syncPull(DaemonServices &services) {
  if (services.remoteSyncService == nullptr ||
      services.syncReconciler == nullptr) {
    throw std::runtime_error("Remote sync is unavailable");
  }

  const auto roots = services.syncService->getSyncRoots();
  if (!services.remoteSyncService->runNow(roots)) {
    throw std::runtime_error("Remote sync is unavailable");
  }

  uint32_t reconciled = 0;
  for (const auto &root : roots) {
    if (!root.enabled) {
      continue;
    }
    services.syncReconciler->reconcileRoot(root);
    reconciled++;
  }

  arkive::ipc::Response response;
  response.set_ok(true);
  response.set_scanned_root_count(reconciled);
  return response;
}

} // namespace ipc::commands
