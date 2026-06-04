#pragma once

#include "api/ArkiveHttpClient.hpp"

#include <cstdint>
#include <stdexcept>

class NullArkiveHttpClient final : public ArkiveHttpClient {
public:
  NullArkiveHttpClient() : ArkiveHttpClient("http://unused", "/tmp/unused") {}

  nlohmann::json postJson(const std::string &, const nlohmann::json &) {
    throw std::runtime_error("NullArkiveHttpClient.postJson called");
  }

  nlohmann::json getJson(const std::string &) {
    throw std::runtime_error("NullArkiveHttpClient.getJson called");
  }

  void postForm(const std::string &) {
    throw std::runtime_error("NullArkiveHttpClient.postForm called");
  }

  std::string putBytes(const std::string &, const std::vector<uint8_t> &) {
    throw std::runtime_error("NullArkiveHttpClient.putBytes called");
  }
};
