#pragma once

#include "ipc/IpcProtocol.hpp"

#include <string>

class IpcProtocolClient {
public:
  explicit IpcProtocolClient(const std::string &endpoint);

  arkive::ipc::Response request(const arkive::ipc::Request &request);

private:
  std::string endpoint_;
};
