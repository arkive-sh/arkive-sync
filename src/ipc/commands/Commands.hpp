#pragma once

#include "ipc/IpcProtocol.hpp"

#include <functional>

struct DaemonServices;

namespace ipc::commands {

arkive::ipc::Response status(DaemonServices &services);
arkive::ipc::Response login(DaemonServices &services,
                            const arkive::ipc::Request &request);
arkive::ipc::Response logout(DaemonServices &services);
arkive::ipc::Response syncAdd(DaemonServices &services,
                              const arkive::ipc::Request &request);
arkive::ipc::Response syncRun(DaemonServices &services);
arkive::ipc::Response syncList(DaemonServices &services);
arkive::ipc::Response syncRemove(DaemonServices &services,
                                 const arkive::ipc::Request &request);
arkive::ipc::Response syncPull(DaemonServices &services);
arkive::ipc::Response stop(std::function<void()> requestStop);

} // namespace ipc::commands
