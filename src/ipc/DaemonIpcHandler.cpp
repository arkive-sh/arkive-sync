#include "ipc/DaemonIpcHandler.hpp"
#include "ipc/commands/Commands.hpp"

DaemonIpcServer::Handler
makeDaemonIpcHandler(DaemonServices &services, std::function<void()> stop) {
  return [&services, stop = std::move(stop)](const arkive::ipc::Request &request) {
    arkive::ipc::Response response;
    response.set_protocol_version(ipc::kProtocolVersion);

    if (!ipc::isSupportedProtocolVersion(request.protocol_version())) {
      response.set_error("Unsupported IPC protocol version");
      return response;
    }

    switch (request.command()) {
    case arkive::ipc::STATUS:
      return ipc::commands::status(services);
    case arkive::ipc::LOGIN:
      return ipc::commands::login(services, request);
    case arkive::ipc::LOGOUT:
      return ipc::commands::logout(services);
    case arkive::ipc::SYNC_ADD:
      return ipc::commands::syncAdd(services, request);
    case arkive::ipc::SYNC_RUN:
      return ipc::commands::syncRun(services);
    case arkive::ipc::SYNC_LIST:
      return ipc::commands::syncList(services);
    case arkive::ipc::SYNC_REMOVE:
      return ipc::commands::syncRemove(services, request);
    case arkive::ipc::SYNC_PULL:
      return ipc::commands::syncPull(services);
    case arkive::ipc::STOP:
      return ipc::commands::stop(stop);
    default:
      response.set_error("Unsupported IPC command");
      return response;
    }
  };
}
