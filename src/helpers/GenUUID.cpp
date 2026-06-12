#include "./GenUUID.hpp"
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

std::string generateUUID() {
  // Use thread-local random tools for performance and thread safety
  static thread_local std::random_device rd;
  static thread_local std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

  std::stringstream ss;
  ss << std::hex << std::setfill('0');

  // Format: 8-4-4-4-12 hex digits
  ss << std::setw(8) << dis(gen)
     << "-"
     /* 4 */
     << std::setw(4) << (dis(gen) & 0xFFFF)
     << "-"
     /* 4 */
     << std::setw(4) << ((dis(gen) & 0x0FFF) | 0x4000)
     << "-" // Variant 4 UUID
            /* 4 */
     << std::setw(4) << ((dis(gen) & 0x3FFF) | 0x8000)
     << "-"
     /* 12 */
     << std::setw(8) << dis(gen) << std::setw(4) << (dis(gen) & 0xFFFF);

  return ss.str();
}
