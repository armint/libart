#include <string>
#include <optional>
#include <doctest/doctest.h>

#include <streams/StringWritable.h>
#include <string>
#include <cstring>

TEST_CASE("[StringWritable] normal write [StringWritable]") {
    char buf[10];
    StringWritable sw(buf);

    auto res = sw.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("Hello"), 5));
    REQUIRE(res);
    REQUIRE(res.value() == 5);
    REQUIRE(sw.position() == 5);
    REQUIRE(std::string(buf, 5) == "Hello");
}

TEST_CASE("[StringWritable] exactly fill buffer [StringWritable]") {
    char buf[6];
    StringWritable sw(buf);

    // Writing 6 bytes into 6 byte buffer.
    // StringWritable should overwrite the 6th byte with \0.
    auto res = sw.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("Hello!"), 6));
    REQUIRE(res);
    REQUIRE(res.value() == 6);
    REQUIRE(sw.position() == 6);
    REQUIRE(buf[5] == '\0');
    REQUIRE(std::string(buf) == "Hello");
}

TEST_CASE("[StringWritable] overflow buffer [StringWritable]") {
    char buf[5];
    StringWritable sw(buf);

    // Writing 10 bytes into 5 byte buffer.
    auto res = sw.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("Hello World"), 11));
    REQUIRE_FALSE(res);
    REQUIRE(res.error() == Writable::Overflow);
    REQUIRE(sw.position() == 5);
    REQUIRE(buf[4] == '\0');
    REQUIRE(std::string(buf) == "Hell");
}

TEST_CASE("[StringWritable] multiple writes [StringWritable]") {
    char buf[10];
    StringWritable sw(buf);

    sw.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("Abc"), 3));
    REQUIRE(sw.position() == 3);
    
    sw.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("Def"), 3));
    REQUIRE(sw.position() == 6);
    
    REQUIRE(std::string(buf, 6) == "AbcDef");

    // Fill it up
    sw.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("GHIJ"), 4));
    REQUIRE(sw.position() == 10);
    REQUIRE(buf[9] == '\0');
    REQUIRE(std::string(buf) == "AbcDefGHI");
}

TEST_CASE("[StringWritable] printf [StringWritable]") {
    char buf[20];
    memset(buf, 'X', sizeof(buf));
    StringWritable sw(buf);

    auto res = sw.printf("Val: %d", 123);
    REQUIRE(res);
    // Note: StringWritable only zero-terminates when the buffer is full.
    // So here we don't expect it to be zero-terminated by StringWritable itself
    // if it's not full.
    REQUIRE(sw.position() == 8);
    REQUIRE(std::string(buf, 8) == "Val: 123");
    REQUIRE(buf[8] == 'X'); // Should still be 'X' because it wasn't full
}

TEST_CASE("[StringWritable] ensure zero-termination when full [StringWritable]") {
    char buf[5];
    memset(buf, 'X', sizeof(buf));
    StringWritable sw(buf);

    sw.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("ABCD"), 4));
    REQUIRE(sw.position() == 4);
    REQUIRE(buf[4] == 'X'); // Not full yet

    sw.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>("E"), 1));
    REQUIRE(sw.position() == 5);
    REQUIRE(buf[4] == '\0'); // Now it's full, should be zero-terminated (overwriting 'E')
    REQUIRE(std::string(buf) == "ABCD");
}
