#pragma once

#include "platform/Ipc.hpp"

class LinuxIpcServer final : public IpcServer {
public:
  explicit LinuxIpcServer(const std::string &endpoint);
  ~LinuxIpcServer() override;

  std::unique_ptr<IpcConnection> accept() override;
  void stop() override;

private:
  std::string endpoint_;
  int fd_{-1};
};

class LinuxIpcClient final : public IpcClient {
public:
  explicit LinuxIpcClient(std::string endpoint);

  std::unique_ptr<IpcConnection> connect() override;

private:
  std::string endpoint_;
};
