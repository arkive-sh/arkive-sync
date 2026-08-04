#include "ipc/commands/Commands.hpp"

#include "platform/DaemonServices.hpp"
#include "service/SyncService.hpp"

namespace ipc::commands {

arkive::ipc::Response syncRemove(DaemonServices &services,
                                 const arkive::ipc::Request &request) {
  services.syncService->removeSyncRoot(request.path());
  arkive::ipc::Response response;
  response.set_ok(true);
  response.set_sync_root_id(request.path());
  return response;
}

} // namespace ipc::commands
