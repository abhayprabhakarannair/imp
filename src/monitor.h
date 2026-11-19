#pragma once
#include <cstdint>

namespace imp {

enum class Power { Unknown, Online, Offline, Waking };

constexpr const char* name(Power p) {
  switch (p) {
    case Power::Online: return "online";
    case Power::Offline: return "offline";
    case Power::Waking: return "waking";
    case Power::Unknown: break;
  }
  return "unknown";
}

struct Transition {
  enum Kind { None, Up, Down, WakeTimeout } kind = None;
  bool after_wake = false;
  std::uint32_t took_ms = 0;
};

// Tracks one machine from ping results. Times are millis() values; all arithmetic is modulo 2^32.
class Monitor {
 public:
  constexpr Monitor(std::uint8_t offline_after, std::uint32_t wake_timeout_ms)
      : offline_after_(offline_after ? offline_after : 1), wake_timeout_ms_(wake_timeout_ms) {}

  constexpr Transition on_ping(bool reachable, std::uint32_t now) {
    Transition t;
    if (reachable) {
      fails_ = 0;
      seen_ = true;
      seen_at_ = now;
      if (waking_) {
        waking_ = false;
        t = {Transition::Up, true, now - wake_at_};
      } else if (power_ == Power::Offline) {
        t.kind = Transition::Up;
      }
      power_ = Power::Online;
      return t;
    }
    if (fails_ < 0xFF) ++fails_;
    if (power_ == Power::Unknown) {
      power_ = Power::Offline;
    } else if (power_ == Power::Online && fails_ >= offline_after_) {
      power_ = Power::Offline;
      t.kind = Transition::Down;
    }
    return t;
  }

  constexpr void on_wake_sent(std::uint32_t now) {
    waking_ = true;
    wake_at_ = now;
  }

  constexpr Transition tick(std::uint32_t now) {
    if (waking_ && now - wake_at_ >= wake_timeout_ms_) {
      waking_ = false;
      return {Transition::WakeTimeout, true, now - wake_at_};
    }
    return {};
  }

  constexpr Power power() const { return waking_ ? Power::Waking : power_; }
  constexpr bool waking() const { return waking_; }
  constexpr bool seen() const { return seen_; }
  constexpr std::uint32_t since_seen_ms(std::uint32_t now) const { return now - seen_at_; }
  constexpr std::uint32_t waking_for_ms(std::uint32_t now) const { return now - wake_at_; }

 private:
  std::uint8_t offline_after_;
  std::uint32_t wake_timeout_ms_;
  Power power_ = Power::Unknown;
  std::uint8_t fails_ = 0;
  bool waking_ = false;
  std::uint32_t wake_at_ = 0;
  bool seen_ = false;
  std::uint32_t seen_at_ = 0;
};

}  // namespace imp
