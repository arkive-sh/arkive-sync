#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace networking {

class StoragePutTransport {
public:
  StoragePutTransport();
  ~StoragePutTransport();

  StoragePutTransport(StoragePutTransport &&other) noexcept;
  StoragePutTransport &operator=(StoragePutTransport &&other) noexcept;

  std::string put(std::string requestUrl, const std::vector<uint8_t> &body);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace networking
