#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "monitor.h"

namespace imp {

// Bounded JSON writer. Output is always NUL-terminated; ok() is false if anything was truncated or the
// nesting was unbalanced, in which case the buffer must not be published.
class JsonWriter {
 public:
  JsonWriter(char* buf, std::size_t cap) : buf_(buf), cap_(cap) {
    if (cap_ == 0) {
      ok_ = false;
      return;
    }
    buf_[0] = '\0';
    open('{');
  }

  JsonWriter& key(std::string_view k) {
    comma();
    quoted(k);
    put(':');
    pending_value_ = true;
    return *this;
  }
  JsonWriter& str(std::string_view k, std::string_view v) { return key(k).value(v); }
  JsonWriter& num(std::string_view k, long long v) { return key(k).value(v); }
  JsonWriter& flag(std::string_view k, bool v) { return key(k).value(v); }

  JsonWriter& value(std::string_view v) {
    element();
    quoted(v);
    return *this;
  }
  JsonWriter& value(const char* v) { return value(std::string_view(v)); }
  JsonWriter& value(long long v) {
    element();
    char tmp[24];
    std::snprintf(tmp, sizeof tmp, "%lld", v);
    write(tmp);
    return *this;
  }
  JsonWriter& value(bool v) {
    element();
    write(v ? "true" : "false");
    return *this;
  }

  JsonWriter& object(std::string_view k) { return key(k).begin('{'); }
  JsonWriter& array(std::string_view k) { return key(k).begin('['); }
  JsonWriter& end() {
    if (depth_ <= 1) {
      ok_ = false;
      return *this;
    }
    put(closers_[--depth_]);
    return *this;
  }

  bool finish() {
    if (depth_ != 1 || pending_value_) ok_ = false;
    if (ok_) put('}');
    depth_ = 0;
    return ok_;
  }
  bool ok() const { return ok_; }
  std::size_t size() const { return len_; }

 private:
  static constexpr std::size_t kMaxDepth = 6;

  JsonWriter& begin(char c) {
    element();
    open(c);
    return *this;
  }
  void open(char c) {
    if (depth_ == kMaxDepth) {
      ok_ = false;
      return;
    }
    put(c);
    closers_[depth_] = c == '{' ? '}' : ']';
    first_[depth_++] = true;
  }
  void element() {
    if (pending_value_) {
      pending_value_ = false;
      return;
    }
    comma();
  }
  void comma() {
    if (depth_ == 0) {
      ok_ = false;
      return;
    }
    if (!first_[depth_ - 1]) put(',');
    first_[depth_ - 1] = false;
  }
  void quoted(std::string_view s) {
    put('"');
    for (char ch : s) {
      auto c = static_cast<unsigned char>(ch);
      if (c == '"' || c == '\\') {
        put('\\');
        put(ch);
      } else if (c < 0x20) {
        char tmp[8];
        std::snprintf(tmp, sizeof tmp, "\\u%04x", c);
        write(tmp);
      } else {
        put(ch);
      }
    }
    put('"');
  }
  void write(const char* s) {
    while (*s) put(*s++);
  }
  void put(char c) {
    if (!ok_ || len_ + 1 >= cap_) {
      ok_ = false;
      return;
    }
    buf_[len_++] = c;
    buf_[len_] = '\0';
  }

  char* buf_;
  std::size_t cap_;
  std::size_t len_ = 0;
  bool ok_ = true;
  bool pending_value_ = false;
  std::size_t depth_ = 0;
  char closers_[kMaxDepth] = {};
  bool first_[kMaxDepth] = {};
};

struct TargetView {
  std::string_view name;
  const Monitor* monitor;
};

inline bool write_meta(char* buf, std::size_t cap, std::string_view version, const TargetView* targets,
                       std::size_t n) {
  JsonWriter w(buf, cap);
  w.str("kind", "imp").str("version", version);
  w.array("commands").value("wake").value("refresh").value("restart").end();
  w.array("wakes");
  for (std::size_t i = 0; i < n; ++i) w.value(targets[i].name);
  w.end();
  return w.finish();
}

inline bool write_state(char* buf, std::size_t cap, std::uint32_t now, std::uint32_t uptime_s, int rssi,
                        std::string_view ip, const TargetView* targets, std::size_t n) {
  JsonWriter w(buf, cap);
  w.num("uptime_s", uptime_s).num("rssi", rssi).str("ip", ip);
  w.object("targets");
  for (std::size_t i = 0; i < n; ++i) {
    const Monitor& m = *targets[i].monitor;
    w.object(targets[i].name).str("power", name(m.power()));
    if (m.seen()) w.num("last_seen_s", m.since_seen_ms(now) / 1000);
    if (m.waking()) w.num("waking_for_s", m.waking_for_ms(now) / 1000);
    w.end();
  }
  w.end();
  return w.finish();
}

}  // namespace imp
