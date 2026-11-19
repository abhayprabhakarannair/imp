#include <doctest/doctest.h>

#include <string>

#include "report.h"

using imp::JsonWriter;
using imp::Monitor;
using imp::TargetView;

TEST_CASE("flat object with escaping") {
  char buf[128];
  JsonWriter w(buf, sizeof buf);
  w.str("a", "x\"y\\z\n").num("n", -5).flag("f", true);
  REQUIRE(w.finish());
  CHECK(std::string(buf) == R"({"a":"x\"y\\z\u000a","n":-5,"f":true})");
}

TEST_CASE("nested objects and arrays") {
  char buf[128];
  JsonWriter w(buf, sizeof buf);
  w.array("l").value("a").value(1LL).value(false).end();
  w.object("o").object("p").num("q", 1).end().end();
  w.array("e").end();
  REQUIRE(w.finish());
  CHECK(std::string(buf) == R"({"l":["a",1,false],"o":{"p":{"q":1}},"e":[]})");
}

TEST_CASE("truncation is reported and the buffer stays terminated") {
  char buf[16];
  JsonWriter w(buf, sizeof buf);
  w.str("key", "a value that does not fit");
  CHECK_FALSE(w.finish());
  CHECK(std::string(buf).size() == sizeof buf - 1);
}

TEST_CASE("exact fit succeeds, one byte less fails") {
  const std::string expected = R"({"k":"v"})";
  std::string exact(expected.size() + 1, '\0');
  JsonWriter ok(exact.data(), exact.size());
  ok.str("k", "v");
  CHECK(ok.finish());
  std::string small(expected.size(), '\0');
  JsonWriter tight(small.data(), small.size());
  tight.str("k", "v");
  CHECK_FALSE(tight.finish());
}

TEST_CASE("unbalanced or dangling structure is rejected") {
  char buf[64];
  JsonWriter open(buf, sizeof buf);
  open.object("o");
  CHECK_FALSE(open.finish());

  JsonWriter extra(buf, sizeof buf);
  extra.end();
  CHECK_FALSE(extra.finish());

  JsonWriter dangling(buf, sizeof buf);
  dangling.key("k");
  CHECK_FALSE(dangling.finish());
}

TEST_CASE("zero capacity") {
  char c = 'x';
  JsonWriter w(&c, 0);
  CHECK_FALSE(w.finish());
  CHECK(c == 'x');
}

TEST_CASE("meta lists commands and wake targets") {
  Monitor a(3, 1000), b(3, 1000);
  const TargetView t[] = {{"devil", &a}, {"homelab-one", &b}};
  char buf[256];
  REQUIRE(imp::write_meta(buf, sizeof buf, "0.1.0", t, 2));
  CHECK(std::string(buf) ==
        R"({"kind":"imp","version":"0.1.0","commands":["wake","refresh","restart"],"wakes":["devil","homelab-one"]})");
}

TEST_CASE("state includes only the fields that apply") {
  Monitor seen(3, 60000), never(3, 60000), waking(3, 60000);
  seen.on_ping(true, 1000);
  never.on_ping(false, 1000);
  waking.on_ping(false, 1000);
  waking.on_wake_sent(5000);
  const TargetView t[] = {{"a", &seen}, {"b", &never}, {"c", &waking}};
  char buf[256];
  REQUIRE(imp::write_state(buf, sizeof buf, 9000, 42, -50, "192.168.0.6", t, 3));
  CHECK(std::string(buf) ==
        R"({"uptime_s":42,"rssi":-50,"ip":"192.168.0.6","targets":{"a":{"power":"online","last_seen_s":8},)"
        R"("b":{"power":"offline"},"c":{"power":"waking","waking_for_s":4}}})");
}

TEST_CASE("state with no targets") {
  char buf[128];
  REQUIRE(imp::write_state(buf, sizeof buf, 0, 0, 0, "", nullptr, 0));
  CHECK(std::string(buf) == R"({"uptime_s":0,"rssi":0,"ip":"","targets":{}})");
}

TEST_CASE("state that does not fit is rejected") {
  Monitor m(3, 1000);
  const TargetView t[] = {{"devil", &m}};
  char buf[40];
  CHECK_FALSE(imp::write_state(buf, sizeof buf, 0, 0, 0, "192.168.0.6", t, 1));
}
