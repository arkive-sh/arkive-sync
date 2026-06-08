#pragma once

#include <cstdint>
#include <string>
#include <vector>

std::string encodeHex(const std::vector<uint8_t> &bytes);
std::vector<uint8_t> decodeHex(const std::string &input);
const std::string generateRandomHex(const std::size_t length);
