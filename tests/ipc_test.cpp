#include "platform/Ipc.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

TEST_CASE("IPC sends and receives framed messages") {
#if defined(_WIN32)
  const std::string endpoint = R"(\\.\pipe\arkive-ipc-test)";
#else
  const auto endpoint =
      (std::filesystem::temp_directory_path() /
       ("arkive-ipc-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".sock"))
          .string();
#endif
  auto server = IpcServer::create(endpoint);
  std::vector<uint8_t> received;

  std::thread serverThread([&] {
    auto connection = server->accept();
    received = connection->receive();
    connection->send({0x6f, 0x6b});
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  auto client = IpcClient::create(endpoint);
  auto connection = client->connect();
  connection->send({0x61, 0x72, 0x6b, 0x69, 0x76, 0x65});
  REQUIRE(connection->receive() == std::vector<uint8_t>{0x6f, 0x6b});

  serverThread.join();
  REQUIRE(received == std::vector<uint8_t>{0x61, 0x72, 0x6b, 0x69, 0x76, 0x65});
}
