#include "ipc/DaemonIpcServer.hpp"

#include "platform/Ipc.hpp"

#include <stdexcept>
#include <utility>

DaemonIpcServer::DaemonIpcServer(std::string endpoint)
    : endpoint_(std::move(endpoint)) {}

DaemonIpcServer::~DaemonIpcServer() { stop(); }

void DaemonIpcServer::start(Handler handler) {
  if (thread_.joinable()) {
    throw std::runtime_error("IPC server is already running");
  }

  stopping_ = false;
  server_ = IpcServer::create(endpoint_);
  thread_ = std::thread([this, handler = std::move(handler)]() mutable {
    serve(std::move(handler));
  });
}

void DaemonIpcServer::serve(Handler handler) {
  while (server_ && !stopping_) {
    try {
      auto connection = server_->accept();
      const auto request =
          ipc::parse<arkive::ipc::Request>(connection->receive());
      arkive::ipc::Response response;
      response.set_protocol_version(ipc::kProtocolVersion);
      try {
        response = handler(request);
      } catch (const std::exception &error) {
        response.set_error(error.what());
      }
      response.set_protocol_version(ipc::kProtocolVersion);
      connection->send(ipc::serialize(response));
      if (request.command() == arkive::ipc::STOP) {
        stopping_ = true;
        server_->stop();
        return;
      }
    } catch (const std::exception &) {
      if (!server_) {
        return;
      }
    }
  }
}

void DaemonIpcServer::stop() {
  stopping_ = true;
  try {
    auto client = IpcClient::create(endpoint_);
    auto connection = client->connect();
  } catch (const std::exception &) {
  }
  if (server_) {
    server_->stop();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  server_.reset();
}
