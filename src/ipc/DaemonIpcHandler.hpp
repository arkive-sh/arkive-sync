#pragma once

#include "ipc/DaemonIpcServer.hpp"

#include <functional>

struct DaemonServices;

DaemonIpcServer::Handler
makeDaemonIpcHandler(DaemonServices &services, std::function<void()> stop);
