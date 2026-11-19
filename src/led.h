#pragma once
#include <cstdint>

#include "monitor.h"

namespace imp {

enum class Link { NoWifi, NoBus, Up };

struct Rgb {
  std::uint8_t r, g, b;
  constexpr bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

constexpr Rgb kOff{0, 0, 0};

// Amber blink: no Wi-Fi. Blue blink: waking. Green/red: target on/off. Dim white: not yet known.
// A slow blink of any of these means the bus is unreachable.
constexpr Rgb led(Link link, Power target, std::uint32_t now, std::uint8_t level) {
  const bool fast_off = (now / 250) % 2;
  const bool slow_off = (now / 1000) % 2;
  const auto third = static_cast<std::uint8_t>(level / 3);
  if (link == Link::NoWifi) return fast_off ? kOff : Rgb{level, third, 0};
  Rgb c{third, third, third};
  if (target == Power::Waking) return fast_off ? kOff : Rgb{0, 0, level};
  if (target == Power::Online) c = {0, level, 0};
  if (target == Power::Offline) c = {level, 0, 0};
  return link == Link::NoBus && slow_off ? kOff : c;
}

}  // namespace imp
