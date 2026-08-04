#pragma once

#include "arkive_ipc.pb.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ipc {

constexpr uint32_t kProtocolVersion = 1;

bool isSupportedProtocolVersion(uint32_t version);

std::vector<uint8_t> serialize(const google::protobuf::Message &message);

template <typename Message>
Message parse(const std::vector<uint8_t> &bytes) {
  Message message;
  if (!message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
    throw std::runtime_error("Invalid IPC protobuf message");
  }
  return message;
}

} // namespace ipc
