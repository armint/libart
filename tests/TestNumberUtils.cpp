#include <doctest/doctest.h>
#include <utils/numberutils.h>
#include <cstdint>

TEST_CASE("stringToInteger - Basic functionality") {
    SUBCASE("Decimal conversion") {
        auto result = stringToInteger<int>("123");
        REQUIRE(result.has_value());
        CHECK(result.value() == 123);
    }

    SUBCASE("Negative decimal") {
        auto result = stringToInteger<int>("-456");
        REQUIRE(result.has_value());
        CHECK(result.value() == -456);
    }

    SUBCASE("Explicit positive sign") {
        auto result = stringToInteger<int>("+789");
        REQUIRE(result.has_value());
        CHECK(result.value() == 789);
    }

    SUBCASE("Zero") {
        auto result = stringToInteger<int>("0");
        REQUIRE(result.has_value());
        CHECK(result.value() == 0);
    }
}

TEST_CASE("stringToInteger - Bases") {
    SUBCASE("Hexadecimal") {
        auto result = stringToInteger<int>("1A", 16);
        REQUIRE(result.has_value());
        CHECK(result.value() == 26);
    }

    SUBCASE("Hexadecimal lowercase") {
        auto result = stringToInteger<int>("ff", 16);
        REQUIRE(result.has_value());
        CHECK(result.value() == 255);
    }

    SUBCASE("Binary") {
        auto result = stringToInteger<int>("1010", 2);
        REQUIRE(result.has_value());
        CHECK(result.value() == 10);
    }

    SUBCASE("Octal") {
        auto result = stringToInteger<int>("77", 8);
        REQUIRE(result.has_value());
        CHECK(result.value() == 63);
    }
}

TEST_CASE("stringToInteger - Error handling") {
    SUBCASE("Malformed input - Empty string") {
        auto result = stringToInteger<int>("");
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == DecodeError::MalformedInput);
    }

    SUBCASE("Malformed input - Only sign") {
        auto result = stringToInteger<int>("-");
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == DecodeError::MalformedInput);
    }

    SUBCASE("Malformed input - Invalid characters") {
        auto result = stringToInteger<int>("12g3");
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == DecodeError::MalformedInput);
    }

    SUBCASE("Malformed input - Character out of base range") {
        auto result = stringToInteger<int>("12", 2);
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == DecodeError::MalformedInput);
    }
}

TEST_CASE("stringToInteger - Overflow") {
    SUBCASE("Unsigned overflow") {
        auto result = stringToInteger<uint8_t>("256");
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == DecodeError::NumberTooBig);
    }

    SUBCASE("Signed overflow positive") {
        auto result = stringToInteger<int8_t>("128");
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == DecodeError::NumberTooBig);
    }

    SUBCASE("Signed overflow negative") {
        auto result = stringToInteger<int8_t>("-129");
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == DecodeError::NumberTooBig);
    }

    SUBCASE("Signed min value (no overflow)") {
        auto result = stringToInteger<int8_t>("-128");
        REQUIRE(result.has_value());
        CHECK(result.value() == -128);
    }

    SUBCASE("Signed max value (no overflow)") {
        auto result = stringToInteger<int8_t>("127");
        REQUIRE(result.has_value());
        CHECK(result.value() == 127);
    }
}
