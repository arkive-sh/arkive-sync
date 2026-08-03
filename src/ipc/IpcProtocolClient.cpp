#include "ipc/IpcProtocolClient.hpp"

#include "platform/Ipc.hpp"

IpcProtocolClient::IpcProtocolClient(const std::string &endpoint)
    : endpoint_(endpoint) {}

arkive::ipc::Response
IpcProtocolClient::request(const arkive::ipc::Request &request) {
  auto client = IpcClient::create(endpoint_);
  auto connection = client->connect();
  connection->send(ipc::serialize(request));
  return ipc::parse<arkive::ipc::Response>(connection->receive());
}
