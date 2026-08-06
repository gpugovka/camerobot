#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace camerobot
{
inline std::string base64_encode(const std::vector<uint8_t> & data)
{
  static constexpr char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  encoded.reserve(((data.size() + 2) / 3) * 4);

  size_t index = 0;
  while (index + 2 < data.size()) {
    const uint32_t chunk =
      (static_cast<uint32_t>(data[index]) << 16) |
      (static_cast<uint32_t>(data[index + 1]) << 8) |
      static_cast<uint32_t>(data[index + 2]);

    encoded.push_back(alphabet[(chunk >> 18) & 0x3F]);
    encoded.push_back(alphabet[(chunk >> 12) & 0x3F]);
    encoded.push_back(alphabet[(chunk >> 6) & 0x3F]);
    encoded.push_back(alphabet[chunk & 0x3F]);
    index += 3;
  }

  const size_t remaining = data.size() - index;
  if (remaining == 1) {
    const uint32_t chunk = static_cast<uint32_t>(data[index]) << 16;
    encoded.push_back(alphabet[(chunk >> 18) & 0x3F]);
    encoded.push_back(alphabet[(chunk >> 12) & 0x3F]);
    encoded.push_back('=');
    encoded.push_back('=');
  } else if (remaining == 2) {
    const uint32_t chunk =
      (static_cast<uint32_t>(data[index]) << 16) |
      (static_cast<uint32_t>(data[index + 1]) << 8);
    encoded.push_back(alphabet[(chunk >> 18) & 0x3F]);
    encoded.push_back(alphabet[(chunk >> 12) & 0x3F]);
    encoded.push_back(alphabet[(chunk >> 6) & 0x3F]);
    encoded.push_back('=');
  }

  return encoded;
}

inline std::vector<uint8_t> base64_decode(const std::string & encoded)
{
  static const std::string alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::vector<uint8_t> decoded;
  decoded.reserve((encoded.size() / 4) * 3);

  uint32_t buffer = 0;
  int bits_collected = 0;

  for (char ch : encoded) {
    if (ch == '=') {
      break;
    }

    const size_t value = alphabet.find(ch);
    if (value == std::string::npos) {
      continue;
    }

    buffer = (buffer << 6) | static_cast<uint32_t>(value);
    bits_collected += 6;

    if (bits_collected >= 8) {
      bits_collected -= 8;
      decoded.push_back(static_cast<uint8_t>((buffer >> bits_collected) & 0xFF));
    }
  }

  return decoded;
}
}  // namespace camerobot