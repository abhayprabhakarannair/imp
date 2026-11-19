#pragma once
#include <cstddef>
#include <string_view>

namespace imp {

enum class Action { Wake, Refresh, Restart };

struct Command {
  Action action;
  std::string_view target;
  std::string_view source;
};

constexpr std::size_t kMaxPayload = 96;
constexpr std::size_t kMaxName = 31;

constexpr bool valid_name(std::string_view s) {
  if (s.empty() || s.size() > kMaxName) return false;
  for (char c : s) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

// Splits on runs of spaces. Returns the number of tokens, or max + 1 if there are more than max.
constexpr std::size_t tokenize(std::string_view s, std::string_view* out, std::size_t max) {
  std::size_t n = 0, i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') ++i;
    if (i == s.size()) break;
    std::size_t start = i;
    while (i < s.size() && s[i] != ' ') ++i;
    if (n == max) return max + 1;
    out[n++] = s.substr(start, i - start);
  }
  return n;
}

// Accepts "wake <target> [source]", "refresh [source]", "restart [source]".
constexpr bool parse(std::string_view payload, Command& out) {
  if (payload.size() > kMaxPayload) return false;
  std::string_view t[3];
  std::size_t n = tokenize(payload, t, 3);
  if (n == 0 || n > 3) return false;

  Command c{Action::Refresh, {}, {}};
  std::size_t next = 1;
  if (t[0] == "wake") {
    if (n < 2 || !valid_name(t[1])) return false;
    c = {Action::Wake, t[1], {}};
    next = 2;
  } else if (t[0] == "refresh") {
    c.action = Action::Refresh;
  } else if (t[0] == "restart") {
    c.action = Action::Restart;
  } else {
    return false;
  }
  if (n > next + 1) return false;
  if (n == next + 1) {
    if (!valid_name(t[next])) return false;
    c.source = t[next];
  }
  out = c;
  return true;
}

}  // namespace imp
