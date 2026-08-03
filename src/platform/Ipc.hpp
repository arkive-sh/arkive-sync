#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class IpcConnection {
public:
  virtual ~IpcConnection() = default;

  IpcConnection() = default;
  IpcConnection(const IpcConnection &) = delete;
  IpcConnection &operator=(const IpcConnection &) = delete;

  virtual void send(const std::vector<uint8_t> &message) = 0;
  virtual std::vector<uint8_t> receive() = 0;
};

class IpcServer {
public:
  virtual ~IpcServer() = default;

  IpcServer() = default;
  IpcServer(const IpcServer &) = delete;
  IpcServer &operator=(const IpcServer &) = delete;

  virtual std::unique_ptr<IpcConnection> accept() = 0;
  virtual void stop() = 0;

  static std::unique_ptr<IpcServer> create(const std::string &endpoint);
};

class IpcClient {
public:
  virtual ~IpcClient() = default;

  IpcClient() = default;
  IpcClient(const IpcClient &) = delete;
  IpcClient &operator=(const IpcClient &) = delete;

  virtual std::unique_ptr<IpcConnection> connect() = 0;

  static std::unique_ptr<IpcClient> create(const std::string &endpoint);
};
