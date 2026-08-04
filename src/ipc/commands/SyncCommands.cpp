#include "ipc/commands/Commands.hpp"

#include "platform/DaemonServices.hpp"
#include "repo/ScanRepo.hpp"
#include "service/SyncService.hpp"
#include "sync/RootScanner.hpp"

#include <stdexcept>

namespace ipc::commands {

arkive::ipc::Response syncAdd(DaemonServices &services,
                              const arkive::ipc::Request &request) {
  const auto root = services.syncService->addSyncRoot(request.path());
  arkive::ipc::Response response;
  response.set_ok(true);
  response.set_sync_root_id(root.Id);
  response.set_sync_root_path(root.localPath);
  return response;
}

arkive::ipc::Response syncRun(DaemonServices &services) {
  const auto roots = services.syncService->getSyncRoots();
  if (roots.empty()) {
    throw std::runtime_error("No sync roots configured");
  }

  uint32_t scanned = 0;
  for (const auto &root : roots) {
    if (!root.enabled) {
      continue;
    }
    while (true) {
      if (!services.rootScanner->scanRoot(root.Id)) {
        throw std::runtime_error("Failed to scan sync root: " + root.Id);
      }
      if (!services.scanRepo->hasRunningScanJob(root.Id)) {
        scanned++;
        break;
      }
    }
  }

  arkive::ipc::Response response;
  response.set_ok(true);
  response.set_scanned_root_count(scanned);
  return response;
}

} // namespace ipc::commands
