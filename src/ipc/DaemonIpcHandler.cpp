#include "ipc/DaemonIpcHandler.hpp"

#include "platform/DaemonServices.hpp"
#include "service/SyncService.hpp"

#include <utility>

DaemonIpcServer::Handler
makeDaemonIpcHandler(DaemonServices &services, std::function<void()> stop) {
  return [&services, stop = std::move(stop)](const arkive::ipc::Request &request) {
    arkive::ipc::Response response;
    response.set_protocol_version(ipc::kProtocolVersion);

    if (request.protocol_version() != ipc::kProtocolVersion) {
      response.set_error("Unsupported IPC protocol version");
      return response;
    }

    switch (request.command()) {
    case arkive::ipc::STATUS:
      response.set_ok(true);
      response.set_state("running");
      response.set_sync_root_count(
          static_cast<uint32_t>(services.syncService->getSyncRoots().size()));
      return response;
    case arkive::ipc::STOP:
      response.set_ok(true);
      response.set_state("stopping");
      stop();
      return response;
    default:
      response.set_error("Unsupported IPC command");
      return response;
    }
  };
}
