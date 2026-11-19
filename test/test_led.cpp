#include <doctest/doctest.h>

#include "led.h"

using imp::led;
using imp::Link;
using imp::Power;
using imp::Rgb;

constexpr std::uint8_t L = 30;
constexpr std::uint32_t ON = 0, FAST_OFF = 250, SLOW_OFF = 1000;

TEST_CASE("steady colours when the bus is up") {
  CHECK(led(Link::Up, Power::Online, FAST_OFF, L) == Rgb{0, L, 0});
  CHECK(led(Link::Up, Power::Offline, SLOW_OFF, L) == Rgb{L, 0, 0});
  CHECK(led(Link::Up, Power::Unknown, SLOW_OFF, L) == Rgb{10, 10, 10});
}

TEST_CASE("no Wi-Fi blinks amber fast regardless of target") {
  CHECK(led(Link::NoWifi, Power::Online, ON, L) == Rgb{L, 10, 0});
  CHECK(led(Link::NoWifi, Power::Waking, FAST_OFF, L) == imp::kOff);
}

TEST_CASE("waking blinks blue fast, even when the bus is down") {
  CHECK(led(Link::Up, Power::Waking, ON, L) == Rgb{0, 0, L});
  CHECK(led(Link::Up, Power::Waking, FAST_OFF, L) == imp::kOff);
  CHECK(led(Link::NoBus, Power::Waking, ON, L) == Rgb{0, 0, L});
}

TEST_CASE("bus unreachable blinks the target colour slowly") {
  CHECK(led(Link::NoBus, Power::Online, ON, L) == Rgb{0, L, 0});
  CHECK(led(Link::NoBus, Power::Online, FAST_OFF, L) == Rgb{0, L, 0});
  CHECK(led(Link::NoBus, Power::Online, SLOW_OFF, L) == imp::kOff);
}

TEST_CASE("zero brightness turns the LED off") {
  CHECK(led(Link::Up, Power::Online, ON, 0) == imp::kOff);
  CHECK(led(Link::NoWifi, Power::Online, ON, 0) == imp::kOff);
}
