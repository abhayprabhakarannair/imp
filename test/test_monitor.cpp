#include <doctest/doctest.h>

#include <cstdint>
#include <string_view>

#include "monitor.h"

using imp::Monitor;
using imp::Power;
using imp::Transition;

TEST_CASE("first result decides the initial state without an event") {
  Monitor up(3, 1000);
  CHECK(up.power() == Power::Unknown);
  CHECK(up.on_ping(true, 10).kind == Transition::None);
  CHECK(up.power() == Power::Online);

  Monitor down(3, 1000);
  CHECK(down.on_ping(false, 10).kind == Transition::None);
  CHECK(down.power() == Power::Offline);
  CHECK_FALSE(down.seen());
}

TEST_CASE("online goes offline only after the threshold of consecutive failures") {
  Monitor m(3, 1000);
  m.on_ping(true, 0);
  CHECK(m.on_ping(false, 1).kind == Transition::None);
  CHECK(m.on_ping(false, 2).kind == Transition::None);
  CHECK(m.power() == Power::Online);
  CHECK(m.on_ping(false, 3).kind == Transition::Down);
  CHECK(m.power() == Power::Offline);
  CHECK(m.on_ping(false, 4).kind == Transition::None);
}

TEST_CASE("a success resets the failure count") {
  Monitor m(3, 1000);
  m.on_ping(true, 0);
  m.on_ping(false, 1);
  m.on_ping(false, 2);
  m.on_ping(true, 3);
  m.on_ping(false, 4);
  m.on_ping(false, 5);
  CHECK(m.power() == Power::Online);
}

TEST_CASE("coming back without a wake is an Up without timing") {
  Monitor m(1, 1000);
  m.on_ping(true, 0);
  m.on_ping(false, 1);
  const Transition t = m.on_ping(true, 50);
  CHECK(t.kind == Transition::Up);
  CHECK_FALSE(t.after_wake);
}

TEST_CASE("wake then up reports how long it took") {
  Monitor m(3, 60000);
  m.on_ping(false, 0);
  m.on_wake_sent(100);
  CHECK(m.power() == Power::Waking);
  CHECK(m.on_ping(false, 2000).kind == Transition::None);
  CHECK(m.power() == Power::Waking);
  const Transition t = m.on_ping(true, 34100);
  CHECK(t.kind == Transition::Up);
  CHECK(t.after_wake);
  CHECK(t.took_ms == 34000);
  CHECK(m.power() == Power::Online);
}

TEST_CASE("wake while it still looked online still waits for a fresh reply") {
  Monitor m(3, 60000);
  m.on_ping(true, 0);
  m.on_wake_sent(10);
  CHECK(m.power() == Power::Waking);
  const Transition t = m.on_ping(true, 2010);
  CHECK(t.kind == Transition::Up);
  CHECK(t.took_ms == 2000);
}

TEST_CASE("wake times out exactly at the deadline") {
  Monitor m(3, 1000);
  m.on_ping(false, 0);
  m.on_wake_sent(0);
  CHECK(m.tick(999).kind == Transition::None);
  const Transition t = m.tick(1000);
  CHECK(t.kind == Transition::WakeTimeout);
  CHECK(m.power() == Power::Offline);
  CHECK(m.tick(5000).kind == Transition::None);
}

TEST_CASE("a second wake restarts the deadline") {
  Monitor m(3, 1000);
  m.on_ping(false, 0);
  m.on_wake_sent(0);
  m.on_wake_sent(800);
  CHECK(m.tick(1500).kind == Transition::None);
  CHECK(m.tick(1800).kind == Transition::WakeTimeout);
}

TEST_CASE("millis() wraparound") {
  const std::uint32_t near_end = 0xFFFFFF00u;
  Monitor m(3, 1000);
  m.on_ping(false, near_end);
  m.on_wake_sent(near_end);
  CHECK(m.tick(near_end + 500).kind == Transition::None);
  const Transition t = m.on_ping(true, near_end + 700);
  CHECK(t.kind == Transition::Up);
  CHECK(t.took_ms == 700);
  CHECK(m.since_seen_ms(near_end + 900) == 200);
}

TEST_CASE("zero threshold behaves as one") {
  Monitor m(0, 1000);
  m.on_ping(true, 0);
  CHECK(m.on_ping(false, 1).kind == Transition::Down);
}

TEST_CASE("failure counter saturates") {
  Monitor m(255, 1000);
  m.on_ping(true, 0);
  for (std::uint32_t i = 1; i <= 1000; ++i) m.on_ping(false, i);
  CHECK(m.power() == Power::Offline);
  CHECK(m.on_ping(true, 2000).kind == Transition::Up);
}

TEST_CASE("state names") {
  CHECK(std::string_view(imp::name(Power::Unknown)) == "unknown");
  CHECK(std::string_view(imp::name(Power::Online)) == "online");
  CHECK(std::string_view(imp::name(Power::Offline)) == "offline");
  CHECK(std::string_view(imp::name(Power::Waking)) == "waking");
}
