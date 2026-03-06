#include <optional>
#include <doctest/doctest.h>
#include <utils/tfp_printf.h>
#include <cstring>
#include <etl/string.h>
#include <etl/string_view.h>

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
    SUBCASE("pointer") {
        // Pointer format %p should be 8 chars with zero padding (in this implementation)
        CHECK(format("%p", (void*)0x1234) == "00001234");
    }
    SUBCASE("width and zero padding") {
        CHECK(format("%04u", 12) == "0012");
        CHECK(format("%4u", 12) == "  12");
    }
}

#if defined(LIBART_ENABLE_64BIT)
TEST_CASE("tfp_printf 64-bit formatting [tfp_printf]") {
    SUBCASE("unsigned 64-bit") {
        CHECK(format("%llu", 12345678901234567890ull) == "12345678901234567890");
    }
    SUBCASE("binary 64-bit") {
        CHECK(format("%llb", 0x100000000ull) == "100000000000000000000000000000000");
    }
    SUBCASE("hex 64-bit") {
        CHECK(format("%llx", 0x123456789abcdef0ull) == "123456789abcdef0");
        CHECK(format("%llX", 0x123456789abcdef0ull) == "123456789ABCDEF0");
    }
}
#endif
