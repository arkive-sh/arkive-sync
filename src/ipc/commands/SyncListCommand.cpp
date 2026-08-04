#include "ipc/commands/Commands.hpp"

#include "platform/DaemonServices.hpp"
#include "service/SyncService.hpp"
#include "sync/SyncMode.hpp"

namespace ipc::commands {

arkive::ipc::Response syncList(DaemonServices &services) {
  arkive::ipc::Response response;
  response.set_ok(true);
  for (const auto &root : services.syncService->getSyncRoots()) {
    auto *output = response.add_sync_roots();
    output->set_id(root.Id);
    output->set_path(root.localPath);
    output->set_enabled(root.enabled != 0);
    output->set_mode(toSyncModeDb(root.mode));
  }
  return response;
}

} // namespace ipc::commands
