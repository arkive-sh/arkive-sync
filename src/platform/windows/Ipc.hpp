#pragma once

#include "platform/Ipc.hpp"

class WindowsIpcServer final : public IpcServer {
public:
  explicit WindowsIpcServer(const std::string &endpoint);
  ~WindowsIpcServer() override;

  std::unique_ptr<IpcConnection> accept() override;
  void stop() override;

private:
  std::string endpoint_;
  void *handle_{nullptr};
};

class WindowsIpcClient final : public IpcClient {
public:
  explicit WindowsIpcClient(std::string endpoint);

  std::unique_ptr<IpcConnection> connect() override;

private:
  std::string endpoint_;
};
