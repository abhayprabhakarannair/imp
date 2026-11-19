#include <doctest/doctest.h>

#include <string>

#include "command.h"

using imp::Action;
using imp::Command;
using imp::parse;

TEST_CASE("wake with target") {
  Command c{};
  REQUIRE(parse("wake devil", c));
  CHECK(c.action == Action::Wake);
  CHECK(c.target == "devil");
  CHECK(c.source.empty());
}

TEST_CASE("wake with target and source, extra spaces tolerated") {
  Command c{};
  REQUIRE(parse("  wake   devil  console ", c));
  CHECK(c.target == "devil");
  CHECK(c.source == "console");
}

TEST_CASE("refresh and restart, with and without source") {
  Command c{};
  REQUIRE(parse("refresh", c));
  CHECK(c.action == Action::Refresh);
  REQUIRE(parse("restart console", c));
  CHECK(c.action == Action::Restart);
  CHECK(c.source == "console");
  CHECK(c.target.empty());
}

TEST_CASE("rejects malformed commands without touching the output") {
  const char* bad[] = {
      "",          "   ",          "wake",          "wake DEVIL",     "wake devil a b", "refresh a b",
      "Wake devil", "wake de/vil", "wake devil x!", "shutdown devil", "wake\tdevil",    "restart now please",
  };
  for (const char* p : bad) {
    Command c{Action::Restart, "unchanged", "unchanged"};
    CHECK_MESSAGE(!parse(p, c), p);
    CHECK(c.target == "unchanged");
  }
}

TEST_CASE("name length limits") {
  Command c{};
  CHECK(parse("wake " + std::string(imp::kMaxName, 'a'), c));
  CHECK_FALSE(parse("wake " + std::string(imp::kMaxName + 1, 'a'), c));
}

TEST_CASE("payload length limit") {
  Command c{};
  std::string padded = "refresh" + std::string(imp::kMaxPayload - 7, ' ');
  CHECK(parse(padded, c));
  CHECK_FALSE(parse(padded + " ", c));
}

TEST_CASE("embedded NUL is not a separator") {
  Command c{};
  CHECK_FALSE(parse(std::string_view("wake devil\0x", 12), c));
}
