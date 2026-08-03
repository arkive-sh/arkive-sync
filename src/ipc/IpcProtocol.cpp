#include "ipc/IpcProtocol.hpp"

#include <stdexcept>

namespace ipc {

std::vector<uint8_t> serialize(const google::protobuf::Message &message) {
  const size_t size = message.ByteSizeLong();
  std::vector<uint8_t> bytes(size);
  if (!message.SerializeToArray(bytes.data(), static_cast<int>(bytes.size()))) {
    throw std::runtime_error("Failed to serialize IPC protobuf message");
  }
  return bytes;
}

} // namespace ipc
