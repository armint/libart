#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <string>
#include <string_view>
#include <utils/FloatText.h>
#include <utils/pch.h>

using utils::FloatText::formatFloat;
using utils::FloatText::kMaxTextLength;
using utils::FloatText::parseFloat;

namespace {

uint32_t bitsOf(float f) {
   return std::bit_cast<uint32_t>(f);
}

float floatFromBits(uint32_t bits) {
   return std::bit_cast<float>(bits);
}

// Formats into a fresh, exactly-sized-enough buffer and returns the written text. Fails the
// current test (via REQUIRE) if the buffer turns out too small.
std::string_view format(float value, std::span<char> buf) {
   size_t len = formatFloat(value, buf);
   REQUIRE(len > 0);
   return { buf.data(), len };
}

} // namespace

TEST_CASE("FloatText::formatFloat - %g-style, from the issue's examples") {
   char buf[kMaxTextLength];

   CHECK(format(100.0f, buf) == "100");
   CHECK(format(0.25f, buf) == "0.25");
   CHECK(format(1.5e-7f, buf) == "1.5e-07");
   CHECK(format(-1.5e-7f, buf) == "-1.5e-07");
}

TEST_CASE("FloatText::formatFloat - fixed vs. scientific threshold") {
   char buf[kMaxTextLength];

   SUBCASE("just inside the small-magnitude fixed range") {
      CHECK(format(0.0001f, buf) == "0.0001");
   }
   SUBCASE("just outside it goes scientific") {
      CHECK(format(0.00001f, buf) == "1e-05");
   }
   SUBCASE("large integers stay fixed below 1e9") {
      CHECK(format(123456789.0f, buf) == "123456790"); // rounds: float has ~7.2 decimal digits
   }
   SUBCASE("1e9 and above goes scientific") {
      CHECK(format(1e9f, buf) == "1e+09");
   }
   SUBCASE("negative exponent is zero-padded to 2 digits") {
      CHECK(format(1e-20f, buf) == "1e-20");
   }
   SUBCASE("positive exponent is zero-padded to 2 digits") {
      CHECK(format(1e20f, buf) == "1e+20");
   }
}

TEST_CASE("FloatText::formatFloat - special values") {
   char buf[kMaxTextLength];

   CHECK(format(0.0f, buf) == "0");
   CHECK(format(-0.0f, buf) == "-0");
   CHECK(format(std::numeric_limits<float>::infinity(), buf) == "inf");
   CHECK(format(-std::numeric_limits<float>::infinity(), buf) == "-inf");
   CHECK(format(std::numeric_limits<float>::quiet_NaN(), buf) == "nan");
   // A signaling NaN is still a NaN: Ryu's copy_special_str only looks at the mantissa being
   // non-zero, not at the signaling bit, and formatFloat must never touch it with float
   // arithmetic (that would quiet it via a trap on some targets).
   CHECK(format(std::numeric_limits<float>::signaling_NaN(), buf) == "nan");
}

TEST_CASE("FloatText::formatFloat - buffer bounds") {
   SUBCASE("exact fit succeeds") {
      char buf[3];
      CHECK(formatFloat(100.0f, buf) == 3);
   }
   SUBCASE("one byte short fails without writing garbage length") {
      char buf[2];
      CHECK(formatFloat(100.0f, buf) == 0);
   }
   SUBCASE("empty destination always fails") {
      CHECK(formatFloat(0.0f, {}) == 0);
   }
   SUBCASE("kMaxTextLength is always enough") {
      char buf[kMaxTextLength];
      // Sample values expected to be near the worst case (9 significant digits).
      for(float v : { 1.2345679e-4f, -1.2345679e-4f, 1.2345679e-40f, -3.4028235e38f }) {
         CHECK(formatFloat(v, buf) > 0);
      }
   }
   SUBCASE("does not write past the requested length") {
      char buf[8] = { '#', '#', '#', '#', '#', '#', '#', '#' };
      size_t len = formatFloat(100.0f, buf); // "100", 3 chars
      REQUIRE(len == 3);
      CHECK(buf[3] == '#');
      CHECK(buf[7] == '#');
   }
}

TEST_CASE("FloatText::parseFloat - basic values") {
   CHECK(parseFloat("100").value() == 100.0f);
   CHECK(parseFloat("0.25").value() == 0.25f);
   CHECK(parseFloat("1.5e-07").value() == doctest::Approx(1.5e-7f));
   CHECK(parseFloat("-1.5e-07").value() == doctest::Approx(-1.5e-7f));
   CHECK(parseFloat("+5").value() == 5.0f);
   CHECK(parseFloat("0").value() == 0.0f);
}

TEST_CASE("FloatText::parseFloat - .5 and 5. forms") {
   CHECK(parseFloat(".5").value() == 0.5f);
   CHECK(parseFloat("5.").value() == 5.0f);
   CHECK(parseFloat("-.5").value() == -0.5f);
   CHECK(parseFloat("-5.").value() == -5.0f);
}

TEST_CASE("FloatText::parseFloat - f/F suffix") {
   CHECK(parseFloat("1.5f").value() == 1.5f);
   CHECK(parseFloat("1.5F").value() == 1.5f);
   CHECK(parseFloat("100f").value() == 100.0f);
   CHECK(parseFloat("1e5f").value() == 1e5f);
}

TEST_CASE("FloatText::parseFloat - inf/nan, case-insensitive") {
   for(std::string_view s : { "inf", "Inf", "INF", "infinity", "Infinity", "INFINITY" }) {
      auto r = parseFloat(s);
      REQUIRE(r.has_value());
      CHECK(*r == std::numeric_limits<float>::infinity());
   }
   for(std::string_view s : { "-inf", "-Inf", "-INFINITY" }) {
      auto r = parseFloat(s);
      REQUIRE(r.has_value());
      CHECK(*r == -std::numeric_limits<float>::infinity());
   }
   for(std::string_view s : { "nan", "NaN", "NAN", "-nan" }) {
      auto r = parseFloat(s);
      REQUIRE(r.has_value());
      CHECK(std::isnan(*r));
   }
}

TEST_CASE("FloatText::parseFloat - signed zero") {
   auto pos = parseFloat("0");
   auto neg = parseFloat("-0");
   REQUIRE(pos.has_value());
   REQUIRE(neg.has_value());
   CHECK(bitsOf(*pos) == bitsOf(0.0f));
   CHECK(bitsOf(*neg) == bitsOf(-0.0f));
   CHECK(bitsOf(*pos) != bitsOf(*neg));
}

TEST_CASE("FloatText::parseFloat - more than 9 significant digits rounds instead of failing") {
   // 1.234567895 has 10 significant digits; s2f itself would reject it outright. The 10th digit
   // is a 5 with nothing after it and the 9th kept digit (9) is odd, so round-half-to-even rounds
   // up: 1.23456790.
   auto r = parseFloat("1.234567895");
   REQUIRE(r.has_value());
   CHECK(*r == doctest::Approx(1.23456790f));

   // Same mantissa but the 9th kept digit (8) is even: round-half-to-even rounds down.
   auto r2 = parseFloat("1.234567885");
   REQUIRE(r2.has_value());
   CHECK(*r2 == doctest::Approx(1.23456788f));

   // A non-zero digit beyond the round digit breaks the tie regardless of parity (sticky bit).
   auto r3 = parseFloat("1.2345678851");
   REQUIRE(r3.has_value());
   CHECK(*r3 == doctest::Approx(1.23456789f));

   // Rounding the kept mantissa up can itself carry all the way out (999999999.5 -> 1.00000000e9).
   auto r4 = parseFloat("999999999.5");
   REQUIRE(r4.has_value());
   CHECK(*r4 == doctest::Approx(1.0e9f));
}

TEST_CASE("FloatText::parseFloat - out of range literals saturate instead of erroring") {
   auto huge = parseFloat("1e400");
   REQUIRE(huge.has_value());
   CHECK(*huge == std::numeric_limits<float>::infinity());

   auto hugeNeg = parseFloat("-1e400");
   REQUIRE(hugeNeg.has_value());
   CHECK(*hugeNeg == -std::numeric_limits<float>::infinity());

   auto tiny = parseFloat("1e-400");
   REQUIRE(tiny.has_value());
   CHECK(bitsOf(*tiny) == bitsOf(0.0f));

   auto tinyNeg = parseFloat("-1e-400");
   REQUIRE(tinyNeg.has_value());
   CHECK(bitsOf(*tinyNeg) == bitsOf(-0.0f));

   // A digit run padded with zeros plus a large negative exponent: still handled without
   // overflowing the internal exponent arithmetic (and without tripping the defensive length
   // cap tested separately below).
   std::string zeros(50, '0');
   auto padded = parseFloat("1" + zeros + "e-500");
   REQUIRE(padded.has_value());
   CHECK(bitsOf(*padded) == bitsOf(0.0f));
}

TEST_CASE("FloatText::parseFloat - rejects malformed input") {
   for(std::string_view s : {
       "",   ".",     "-",  "+",  "e5",  ".e5", "1.2.3", "1e",   "1e+",  "1e-",  "abc",
       "1x", "1.5ff", "1 ", " 1", "1,5", "--5", "1e5x",  "1.5e", "infi", "nano", "1einf",
       }) {
      INFO("input: '", s, "'");
      auto r = parseFloat(s);
      CHECK_FALSE(r.has_value());
      if(!r.has_value()) {
         CHECK(r.error() == DecodeError::MalformedInput);
      }
   }
}

TEST_CASE("FloatText::parseFloat - rejects an absurdly long literal defensively") {
   std::string tooLong(65, '1');
   auto r = parseFloat(tooLong);
   CHECK_FALSE(r.has_value());
}

TEST_CASE("FloatText - structural round-trip over every exponent") {
   // Every biased exponent (0..255, i.e. every subnormal/normal/special class) crossed with the
   // minimum, maximum and one arbitrary mantissa, and both signs: cheap, deterministic coverage
   // of the boundaries a purely random sample is unlikely to hit.
   char buf[kMaxTextLength];
   for(uint32_t exponent = 0; exponent <= 0xFF; ++exponent) {
      for(uint32_t mantissa : { 0x000000u, 0x7FFFFFu, 0x2A2A2Au }) {
         for(uint32_t sign : { 0u, 1u }) {
            const uint32_t bits = (sign << 31) | (exponent << 23) | mantissa;
            const float value = floatFromBits(bits);

            size_t len = formatFloat(value, buf);
            REQUIRE(len > 0);
            REQUIRE(len <= kMaxTextLength);

            auto parsed = parseFloat({ buf, len });
            REQUIRE(parsed.has_value());
            if(std::isnan(value)) {
               CHECK(std::isnan(*parsed));
            } else {
               CHECK(bitsOf(*parsed) == bits);
            }
         }
      }
   }
}

TEST_CASE("FloatText - sampled round-trip (subset of the exhaustive 2^32 check)") {
   // A fixed seed keeps a failure reproducible. This is the "sampled subset" the issue asks for
   // in the normal test run; TestFloatText.cpp's exhaustive case below covers every bit pattern
   // but only runs in the dedicated CI job (it is doctest::skip()'d here).
   pcg::pcg32 rng(0xF10A7u, 1);
   char buf[kMaxTextLength];

   for(int i = 0; i < 2'000'000; ++i) {
      const uint32_t bits = rng.next();
      const float value = floatFromBits(bits);

      size_t len = formatFloat(value, buf);
      REQUIRE(len > 0);
      REQUIRE(len <= kMaxTextLength);

      auto parsed = parseFloat({ buf, len });
      REQUIRE(parsed.has_value());
      if(std::isnan(value)) {
         CHECK(std::isnan(*parsed));
      } else {
         CHECK(bitsOf(*parsed) == bits);
      }
   }
}

// Walks every one of the 2^32 float bit patterns and checks that formatFloat() followed by
// parseFloat() reproduces the original value exactly (NaN is only checked as "still NaN": Ryu
// canonicalizes every NaN payload to the same "nan" text, so the payload itself does not
// round-trip, which is fine - see FloatText.h). Skipped by default: even at the -O2 this
// translation unit is built at (see tests/CMakeLists.txt), walking 2^32 values through a
// format+parse round trip takes minutes. Run explicitly with:
//   ./libArT-tests --test-case="*exhaustive*" --no-skip=true
TEST_CASE("FloatText - exhaustive round-trip (every 2^32 bit pattern)" * doctest::skip()) {
   char buf[kMaxTextLength];
   uint64_t mismatches = 0;
   uint32_t firstMismatchBits = 0;
   size_t maxLen = 0;

   for(uint64_t i = 0; i <= 0xFFFFFFFFull; ++i) {
      const uint32_t bits = static_cast<uint32_t>(i);
      const float value = floatFromBits(bits);

      const size_t len = formatFloat(value, buf);
      if(len == 0 || len > kMaxTextLength) {
         if(mismatches++ == 0) {
            firstMismatchBits = bits;
         }
         continue;
      }
      if(len > maxLen) {
         maxLen = len;
      }

      const auto parsed = parseFloat(std::string_view(buf, len));
      const bool ok =
      std::isnan(value) ? (parsed.has_value() && std::isnan(*parsed)) : (parsed.has_value() && bitsOf(*parsed) == bits);
      if(!ok) {
         if(mismatches++ == 0) {
            firstMismatchBits = bits;
         }
      }
   }

   INFO("first mismatching bit pattern: ", firstMismatchBits);
   CHECK(mismatches == 0);
   // kMaxTextLength is meant to be a tight bound, not just a safe one.
   CHECK(maxLen == kMaxTextLength);
}
