#include "platform/Ipc.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

int main() {
#if defined(_WIN32)
  const std::string endpoint =
      R"(\\.\pipe\arkive-ipc-smoke-2026)";
#else
  const std::string endpoint =
      (std::filesystem::temp_directory_path() /
       ("arkive-ipc-smoke-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".sock"))
          .string();
#endif

  try {
    auto server = IpcServer::create(endpoint);
    std::vector<uint8_t> received;
    std::exception_ptr serverError;

    std::thread serverThread([&] {
      try {
        auto connection = server->accept();
        received = connection->receive();
        connection->send({0x6f, 0x6b});
      } catch (...) {
        serverError = std::current_exception();
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    auto client = IpcClient::create(endpoint);
    auto connection = client->connect();
    connection->send({0x61, 0x72, 0x6b, 0x69, 0x76, 0x65});
    if (connection->receive() != std::vector<uint8_t>{0x6f, 0x6b}) {
      throw std::runtime_error("IPC response mismatch");
    }

    serverThread.join();
    if (serverError != nullptr) {
      std::rethrow_exception(serverError);
    }
    if (received != std::vector<uint8_t>{0x61, 0x72, 0x6b, 0x69, 0x76, 0x65}) {
      throw std::runtime_error("IPC request mismatch");
    }
  } catch (const std::exception &error) {
    std::cerr << "IPC smoke failed: " << error.what() << '\n';
    return 1;
  }

  std::cout << "IPC smoke passed\n";
  return 0;
}
