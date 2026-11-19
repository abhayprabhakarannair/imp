#include <doctest/doctest.h>

#include "wol.h"

TEST_CASE("magic packet is 6 x 0xFF followed by the MAC 16 times") {
  const imp::Mac mac{0x02, 0x00, 0x5E, 0x10, 0x20, 0x30};
  const auto p = imp::magic_packet(mac);
  REQUIRE(p.size() == 102);
  for (std::size_t i = 0; i < 6; ++i) CHECK(p[i] == 0xFF);
  for (std::size_t rep = 0; rep < 16; ++rep)
    for (std::size_t i = 0; i < 6; ++i) CHECK(p[6 + rep * 6 + i] == mac[i]);
}

TEST_CASE("magic packet is usable at compile time") {
  constexpr auto p = imp::magic_packet({1, 2, 3, 4, 5, 6});
  static_assert(p[0] == 0xFF && p[6] == 1 && p[101] == 6);
}
