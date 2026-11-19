#pragma once
#include <array>
#include <cstdint>

namespace imp {

using Mac = std::array<std::uint8_t, 6>;
using MagicPacket = std::array<std::uint8_t, 6 + 16 * 6>;

constexpr MagicPacket magic_packet(const Mac& mac) {
  MagicPacket p{};
  for (std::size_t i = 0; i < 6; ++i) p[i] = 0xFF;
  for (std::size_t rep = 0; rep < 16; ++rep)
    for (std::size_t i = 0; i < 6; ++i) p[6 + rep * 6 + i] = mac[i];
  return p;
}

}  // namespace imp
