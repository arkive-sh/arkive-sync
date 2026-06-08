#include "helpers/Hex.hpp"

#include <random>
#include <stdexcept>

namespace {

int decodeHexNibble(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return -1;
}

} // namespace

std::string encodeHex(const std::vector<uint8_t> &bytes) {
  static constexpr char kHex[] = "0123456789abcdef";

  std::string encoded;
  encoded.reserve(bytes.size() * 2);
  for (uint8_t byte : bytes) {
    encoded.push_back(kHex[(byte >> 4) & 0x0F]);
    encoded.push_back(kHex[byte & 0x0F]);
  }
  return encoded;
}

std::vector<uint8_t> decodeHex(const std::string &input) {
  if ((input.size() % 2) != 0) {
    throw std::invalid_argument("Invalid hex input length");
  }

  std::vector<uint8_t> output;
  output.reserve(input.size() / 2);
  for (std::size_t index = 0; index < input.size(); index += 2) {
    const int high = decodeHexNibble(input[index]);
    const int low = decodeHexNibble(input[index + 1]);
    if (high < 0 || low < 0) {
      throw std::invalid_argument("Invalid hex input");
    }

    output.push_back(static_cast<uint8_t>((high << 4) | low));
  }

  return output;
}

const std::string generateRandomHex(const std::size_t length) {
  const char hexChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

  std::random_device rd;
  std::mt19937 generator(rd());

  std::uniform_int_distribution<> distribution(0, 35);

  std::string hexString;
  hexString.reserve(length); // Optimize memory allocation

  for (size_t i = 0; i < length; ++i) {
    hexString += hexChars[distribution(generator)];
  }

  return hexString;
}
