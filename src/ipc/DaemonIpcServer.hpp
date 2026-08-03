#pragma once

#include "ipc/IpcProtocol.hpp"
#include "platform/Ipc.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

class DaemonIpcServer {
public:
  using Handler =
      std::function<arkive::ipc::Response(const arkive::ipc::Request &)>;

  explicit DaemonIpcServer(std::string endpoint);
  ~DaemonIpcServer();

  void start(Handler handler);
  void stop();

private:
  void serve(Handler handler);

  std::string endpoint_;
  std::unique_ptr<IpcServer> server_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
};
