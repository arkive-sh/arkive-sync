#include "platform/linux/Ipc.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <stdexcept>
#include <system_error>

namespace {

constexpr uint32_t kMaxMessageSize = 16 * 1024 * 1024;

class UnixConnection final : public IpcConnection {
public:
  explicit UnixConnection(int fd) : fd_(fd) {}
  ~UnixConnection() override {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  void send(const std::vector<uint8_t> &message) override {
    if (message.size() > kMaxMessageSize) {
      throw std::invalid_argument("IPC message exceeds 16 MiB");
    }

    const uint32_t size = htonl(static_cast<uint32_t>(message.size()));
    writeAll(&size, sizeof(size));
    if (!message.empty()) {
      writeAll(message.data(), message.size());
    }
  }

  std::vector<uint8_t> receive() override {
    uint32_t networkSize = 0;
    readAll(&networkSize, sizeof(networkSize));
    const uint32_t size = ntohl(networkSize);
    if (size > kMaxMessageSize) {
      throw std::runtime_error("IPC message exceeds 16 MiB");
    }

    std::vector<uint8_t> message(size);
    if (size != 0) {
      readAll(message.data(), size);
    }
    return message;
  }

private:
  void writeAll(const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    while (size != 0) {
      const ssize_t written = ::write(fd_, bytes, size);
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written <= 0) {
        throw std::system_error(errno, std::generic_category(),
                                "IPC write failed");
      }
      bytes += written;
      size -= static_cast<size_t>(written);
    }
  }

  void readAll(void *data, size_t size) {
    auto *bytes = static_cast<uint8_t *>(data);
    while (size != 0) {
      const ssize_t read = ::read(fd_, bytes, size);
      if (read < 0 && errno == EINTR) {
        continue;
      }
      if (read <= 0) {
        throw std::system_error(read == 0 ? ECONNRESET : errno,
                                std::generic_category(), "IPC read failed");
      }
      bytes += read;
      size -= static_cast<size_t>(read);
    }
  }

  int fd_;
};

sockaddr_un makeAddress(const std::string &endpoint) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (endpoint.size() >= sizeof(address.sun_path)) {
    throw std::invalid_argument("IPC endpoint path is too long");
  }
  std::strncpy(address.sun_path, endpoint.c_str(), sizeof(address.sun_path) - 1);
  return address;
}

int connectSocket(const std::string &endpoint) {
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    throw std::system_error(errno, std::generic_category(), "IPC socket failed");
  }

  const sockaddr_un address = makeAddress(endpoint);
  if (::connect(fd, reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) < 0) {
    const int error = errno;
    close(fd);
    throw std::system_error(error, std::generic_category(),
                            "IPC connect failed");
  }
  return fd;
}

} // namespace

LinuxIpcServer::LinuxIpcServer(const std::string &endpoint)
    : endpoint_(endpoint) {
  const auto parent = std::filesystem::path(endpoint_).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  unlink(endpoint_.c_str());

  fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd_ < 0) {
    throw std::system_error(errno, std::generic_category(), "IPC socket failed");
  }

  const sockaddr_un address = makeAddress(endpoint_);
  if (bind(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0) {
    const int error = errno;
    stop();
    throw std::system_error(error, std::generic_category(),
                            "IPC bind failed");
  }
  if (listen(fd_, 8) < 0) {
    const int error = errno;
    stop();
    throw std::system_error(error, std::generic_category(),
                            "IPC listen failed");
  }
  if (chmod(endpoint_.c_str(), 0600) < 0) {
    const int error = errno;
    stop();
    throw std::system_error(error, std::generic_category(),
                            "IPC socket permission setup failed");
  }
}

LinuxIpcServer::~LinuxIpcServer() { stop(); }

std::unique_ptr<IpcConnection> LinuxIpcServer::accept() {
  const int connection = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
  if (connection < 0) {
    throw std::system_error(errno, std::generic_category(), "IPC accept failed");
  }
  return std::make_unique<UnixConnection>(connection);
}

void LinuxIpcServer::stop() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  if (!endpoint_.empty()) {
    unlink(endpoint_.c_str());
  }
}

LinuxIpcClient::LinuxIpcClient(std::string endpoint)
    : endpoint_(std::move(endpoint)) {}

std::unique_ptr<IpcConnection> LinuxIpcClient::connect() {
  return std::make_unique<UnixConnection>(connectSocket(endpoint_));
}
