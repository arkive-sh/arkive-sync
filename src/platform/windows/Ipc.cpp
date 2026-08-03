#include "platform/windows/Ipc.hpp"

#include <windows.h>

#include <stdexcept>
#include <utility>

namespace {

constexpr uint32_t kMaxMessageSize = 16 * 1024 * 1024;

class PipeConnection final : public IpcConnection {
public:
  explicit PipeConnection(HANDLE handle) : handle_(handle) {}
  ~PipeConnection() override {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

  void send(const std::vector<uint8_t> &message) override {
    if (message.size() > kMaxMessageSize) {
      throw std::invalid_argument("IPC message exceeds 16 MiB");
    }
    const uint32_t size = static_cast<uint32_t>(message.size());
    write(&size, sizeof(size));
    write(message.data(), static_cast<DWORD>(message.size()));
  }

  std::vector<uint8_t> receive() override {
    uint32_t size = 0;
    read(&size, sizeof(size));
    if (size > kMaxMessageSize) {
      throw std::runtime_error("IPC message exceeds 16 MiB");
    }
    std::vector<uint8_t> message(size);
    if (size != 0) {
      read(message.data(), size);
    }
    return message;
  }

private:
  void write(const void *data, DWORD size) {
    auto *bytes = static_cast<const uint8_t *>(data);
    while (size != 0) {
      DWORD written = 0;
      if (!WriteFile(handle_, bytes, size, &written, nullptr) || written == 0) {
        throw std::runtime_error("IPC pipe write failed");
      }
      bytes += written;
      size -= written;
    }
  }

  void read(void *data, DWORD size) {
    auto *bytes = static_cast<uint8_t *>(data);
    while (size != 0) {
      DWORD readBytes = 0;
      if (!ReadFile(handle_, bytes, size, &readBytes, nullptr) ||
          readBytes == 0) {
        throw std::runtime_error("IPC pipe read failed");
      }
      bytes += readBytes;
      size -= readBytes;
    }
  }

  HANDLE handle_;
};

} // namespace

WindowsIpcServer::WindowsIpcServer(const std::string &endpoint)
    : endpoint_(endpoint) {}

WindowsIpcServer::~WindowsIpcServer() { stop(); }

std::unique_ptr<IpcConnection> WindowsIpcServer::accept() {
  const HANDLE pipe = CreateNamedPipeA(
      endpoint_.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
          PIPE_WAIT, 1, 16 * 1024 * 1024, 16 * 1024 * 1024, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("IPC named pipe creation failed");
  }
  handle_ = pipe;
  if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    handle_ = nullptr;
    throw std::runtime_error("IPC named pipe accept failed");
  }
  handle_ = nullptr;
  return std::make_unique<PipeConnection>(pipe);
}

void WindowsIpcServer::stop() {
  if (handle_ != nullptr) {
    DisconnectNamedPipe(static_cast<HANDLE>(handle_));
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
}

WindowsIpcClient::WindowsIpcClient(std::string endpoint)
    : endpoint_(std::move(endpoint)) {}

std::unique_ptr<IpcConnection> WindowsIpcClient::connect() {
  const HANDLE pipe = CreateFileA(endpoint_.c_str(), GENERIC_READ | GENERIC_WRITE,
                                  0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("IPC named pipe connect failed");
  }
  return std::make_unique<PipeConnection>(pipe);
}
