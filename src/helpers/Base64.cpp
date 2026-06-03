#include "helpers/Base64.hpp"

#include <cctype>
#include <stdexcept>

namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeBase64Char(char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A';
  }
  if (ch >= 'a' && ch <= 'z') {
    return ch - 'a' + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return ch - '0' + 52;
  }
  if (ch == '+') {
    return 62;
  }
  if (ch == '/') {
    return 63;
  }
  return -1;
}

} // namespace

std::string encodeBase64(const std::vector<uint8_t> &bytes) {
  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);

  std::size_t index = 0;
  while (index + 3 <= bytes.size()) {
    const uint32_t block = (static_cast<uint32_t>(bytes[index]) << 16) |
                           (static_cast<uint32_t>(bytes[index + 1]) << 8) |
                           static_cast<uint32_t>(bytes[index + 2]);
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
    encoded.push_back(kBase64Alphabet[block & 0x3F]);
    index += 3;
  }

  const std::size_t remainder = bytes.size() - index;
  if (remainder == 1) {
    const uint32_t block = static_cast<uint32_t>(bytes[index]) << 16;
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back('=');
    encoded.push_back('=');
  } else if (remainder == 2) {
    const uint32_t block = (static_cast<uint32_t>(bytes[index]) << 16) |
                           (static_cast<uint32_t>(bytes[index + 1]) << 8);
    encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
    encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
    encoded.push_back('=');
  }

  return encoded;
}

std::vector<uint8_t> decodeBase64(const std::string &input) {
  std::vector<uint8_t> output;
  int val = 0;
  int valb = -8;

  for (unsigned char rawCh : input) {
    const char ch = static_cast<char>(rawCh);
    if (std::isspace(rawCh)) {
      continue;
    }
    if (ch == '=') {
      break;
    }

    const int decoded = decodeBase64Char(ch);
    if (decoded < 0) {
      throw std::invalid_argument("Invalid base64 input");
    }

    val = (val << 6) + decoded;
    valb += 6;
    if (valb >= 0) {
      output.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }

  return output;
}
