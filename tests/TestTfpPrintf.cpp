#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <etl/string.h>
#include <etl/string_view.h>
#include <limits>
#include <optional>
#include <utils/tfp_printf.h>

static void putf(void *p, char c) {
   auto *s = static_cast<etl::istring *>(p);
   s->append(1, c);
}

static etl::string<64> format(const char *fmt, ...) {
   etl::string<64> s;
   va_list va;
   va_start(va, fmt);
   tfp_format(&s, putf, fmt, va);
   va_end(va);
   return s;
}

TEST_CASE("tfp_printf basic formatting [tfp_printf]") {
   SUBCASE("unsigned decimal") {
      CHECK(format("%u", 0) == "0");
      CHECK(format("%u", 123) == "123");
      CHECK(format("%u", 4294967295u) == "4294967295");
   }
   SUBCASE("binary") {
      CHECK(format("%b", 0) == "0");
      CHECK(format("%b", 5) == "101");
      CHECK(format("%b", 255) == "11111111");
   }
   SUBCASE("hexadecimal") {
      CHECK(format("%x", 0) == "0");
      CHECK(format("%x", 0xabc) == "abc");
      CHECK(format("%X", 0xabc) == "ABC");
   }
   SUBCASE("signed decimal") {
      CHECK(format("%d", 0) == "0");
      CHECK(format("%d", 123) == "123");
      CHECK(format("%d", -123) == "-123");
      CHECK(format("%i", -456) == "-456");
   }
   SUBCASE("negative zero-padded width keeps the sign ahead of the zeros") {
      // Regression: putchw() used to zero-pad in front of the whole
      // string, sign included, giving "000-5" instead of "-0005".
      CHECK(format("%05d", -5) == "-0005");
   }
   SUBCASE("pointer") {
      // Pointer format %p should be 8 chars with zero padding (in this implementation)
      CHECK(format("%p", (void *)0x1234) == "00001234");
   }
   SUBCASE("width and zero padding") {
      CHECK(format("%04u", 12) == "0012");
      CHECK(format("%4u", 12) == "  12");
   }
}

#if defined(PRINTF_ENABLE_FLOATING_POINT)
TEST_CASE("tfp_printf %f formatting [tfp_printf]") {
   SUBCASE("sign") {
      CHECK(format("%.3f", 3.5f) == "3.500");
      CHECK(format("%.3f", -3.5f) == "-3.500");
   }
   SUBCASE("negative magnitude below 1 keeps its sign") {
      // Regression: this used to lose the sign and print the fraction
      // itself as a negative number, e.g. "0.-500".
      CHECK(format("%.3f", -0.5f) == "-0.500");
      CHECK(format("%.1f", -0.5f) == "-0.5");
   }
   SUBCASE("zero and negative zero") {
      CHECK(format("%.3f", 0.0f) == "0.000");
      CHECK(format("%.3f", -0.0f) == "-0.000");
   }
   SUBCASE("rounds the last decimal instead of truncating") {
      // 2.125 is exact in binary float; round-half-up turns .125 into .13.
      CHECK(format("%.2f", 2.125f) == "2.13");
   }
   SUBCASE("rounding carries into the integer part") {
      // 1 - 2^-12, exact in binary float, rounds up to 1.00 at 2 decimals.
      CHECK(format("%.2f", 0.999755859375f) == "1.00");
      CHECK(format("%.2f", -0.999755859375f) == "-1.00");
   }
   SUBCASE("rounding does not overshoot at the float +0.5 boundary") {
      // 0.49999997f is the float just below 0.5f. Computing the round-up
      // decision as (frac * scale + 0.5f) truncated is wrong here: the
      // addition itself rounds the intermediate sum up to exactly 1.0f,
      // which would incorrectly carry into the integer part. Rounding
      // from the leftover (scaled - truncated) instead of adding 0.5f
      // avoids that.
      CHECK(format("%.0f", 0.49999997f) == "0.0");
   }
   SUBCASE("large values") {
      // Exact in binary float: needs ~21 significant bits.
      CHECK(format("%.1f", 1000000.5f) == "1000000.5");
   }
   SUBCASE("values outside the integer range print a defined overflow marker instead of UB") {
      CHECK(format("%.2f", 1e30f) == "ovf");
      CHECK(format("%.2f", -1e30f) == "-ovf");
   }
   SUBCASE("integer-range boundary depends on big_uint_t's width") {
      // Runs under both libArT-tests-float (32-bit) and
      // libArT-tests-float-64bit, exercising the 2^32 and 2^64 overflow
      // boundaries respectively.
#if defined(LIBART_ENABLE_64BIT)
      CHECK(format("%.0f", 5000000000.0f) == "5000000000.0");
#else
      CHECK(format("%.0f", 5000000000.0f) == "ovf");
#endif
   }
   SUBCASE("inf and nan") {
      CHECK(format("%f", std::numeric_limits<float>::infinity()) == "inf");
      CHECK(format("%f", -std::numeric_limits<float>::infinity()) == "-inf");
      CHECK(format("%f", std::numeric_limits<float>::quiet_NaN()) == "nan");
   }
   SUBCASE("precision") {
      // decimals=0 still emits one fractional digit: the pre-existing
      // %.Nf output shape (int part + '.' + N digits) is kept as-is, N=0
      // isn't special-cased into dropping the point and digit.
      CHECK(format("%.0f", 3.0f) == "3.0");
      CHECK(format("%.5f", 3.5f) == "3.50000");
   }
   SUBCASE("precision is capped at 9 decimals regardless of big_uint_t's width") {
      // Beyond 9 decimals the requested precision is clamped so the
      // integer power-of-ten scale cannot overflow big_uint_t; requesting
      // more than that must not change the output.
      CHECK(format("%.9f", 0.5f) == format("%.20f", 0.5f));
      // 0.5 and 1e9 are both exact in binary float, so this is exact too.
      CHECK(format("%.20f", 0.5f) == "0.500000000");
   }
   SUBCASE("width and zero padding apply to the integer part only") {
      CHECK(format("%08.2f", 3.5f) == "00000003.50");
      CHECK(format("%8.2f", 3.5f) == "       3.50");
   }
   SUBCASE("width and zero padding on negative values keep the sign with the digits") {
      // Space padding: the sign already sits ahead of the digits in bf,
      // so right-justifying with spaces needs no special handling.
      CHECK(format("%8.2f", -3.5f) == "      -3.50");
      // Zero padding: the sign must stay ahead of the zeros, not behind
      // them (putchw() fix), so the field reads "-0000003", not
      // "000000-3".
      CHECK(format("%08.2f", -3.5f) == "-0000003.50");
   }
}
#endif

#if defined(LIBART_ENABLE_64BIT)
TEST_CASE("tfp_printf 64-bit formatting [tfp_printf]") {
   SUBCASE("unsigned 64-bit") {
      CHECK(format("%llu", 12345678901234567890ull) == "12345678901234567890");
   }
   SUBCASE("unsigned 64-bit edge values") {
      CHECK(format("%llu", 0ull) == "0");
      CHECK(format("%llu", UINT64_MAX) == "18446744073709551615");
   }
   SUBCASE("binary 64-bit") {
      CHECK(format("%llb", 0x100000000ull) == "100000000000000000000000000000000");
   }
   SUBCASE("hex 64-bit") {
      CHECK(format("%llx", 0x123456789abcdef0ull) == "123456789abcdef0");
      CHECK(format("%llX", 0x123456789abcdef0ull) == "123456789ABCDEF0");
   }
   SUBCASE("hex 64-bit width and zero padding") {
      CHECK(format("%020llx", 0x123456789abcdef0ull) == "0000123456789abcdef0");
   }
   SUBCASE("signed 64-bit") {
      CHECK(format("%lld", INT64_MIN) == "-9223372036854775808");
      CHECK(format("%lli", (int64_t)-42) == "-42");
   }
}
#endif
