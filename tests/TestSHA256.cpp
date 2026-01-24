#include <catch2/catch_test_macros.hpp>
#include "utils/SHA256Digest.h"
#include "streams/DigestWritable.h"
#include "streams/DigestReadable.h"
#include "streams/ArrayWritable.h"
#include "streams/ArrayReadable.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <cstring>

static std::string to_hex(std::span<const uint8_t> data) {
   std::stringstream ss;
   ss << std::hex << std::setfill('0');
   for(auto b : data) {
      ss << std::setw(2) << static_cast<int>(b);
   }
   return ss.str();
}

TEST_CASE("SHA256Digest basic cases", "[sha256]") {
   SHA256Digest sha;

   SECTION("Empty input") {
      std::array<uint8_t, 32> out{};
      auto res = sha.getDigest(out);
      REQUIRE(res.has_value());
      REQUIRE(res.value() == 32);
      // SHA-256 of empty string: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
      REQUIRE(to_hex(out) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
   }

   SECTION("Known message: 'abc'") {
      const char *msg = "abc";
      sha.update(std::span{ reinterpret_cast<const uint8_t *>(msg), 3 });
      std::array<uint8_t, 32> out{};
      auto res = sha.getDigest(out);
      REQUIRE(res.has_value());
      // SHA-256 of "abc": ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
      REQUIRE(to_hex(out) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
   }

   SECTION("Known message: 'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'") {
      const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
      sha.update(std::span{ reinterpret_cast<const uint8_t *>(msg), strlen(msg) });
      std::array<uint8_t, 32> out{};
      auto res = sha.getDigest(out);
      REQUIRE(res.has_value());
      // SHA-256: 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
      REQUIRE(to_hex(out) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
   }
}

TEST_CASE("SHA256Digest chunked updates", "[sha256]") {
   SHA256Digest sha;
   const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
   size_t len = strlen(msg);
   
   for(size_t i = 0; i < len; ++i) {
      sha.update(static_cast<uint8_t>(msg[i]));
   }

   std::array<uint8_t, 32> out{};
   auto res = sha.getDigest(out);
   REQUIRE(res.has_value());
   REQUIRE(to_hex(out) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA256Digest reset", "[sha256]") {
   SHA256Digest sha;
   sha.update(std::span{ reinterpret_cast<const uint8_t *>("abc"), 3 });
   sha.reset();
   
   std::array<uint8_t, 32> out{};
   auto res = sha.getDigest(out);
   REQUIRE(res.has_value());
   REQUIRE(to_hex(out) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA256Digest with DigestWritable", "[sha256][streams]") {
   SHA256Digest sha;
   uint8_t sinkBuffer[128];
   ArrayWritable sink(sinkBuffer);
   DigestWritable dw(sink, sha);

   const char *msg = "abc";
   auto res = dw.write(std::span{ reinterpret_cast<const uint8_t *>(msg), 3 });
   REQUIRE(res.has_value());
   REQUIRE(res.value() == 3);

   // Check that data was written to the sink
   REQUIRE(memcmp(sinkBuffer, msg, 3) == 0);

   // Check the digest
   std::array<uint8_t, 32> out{};
   auto digestRes = sha.getDigest(out);
   REQUIRE(digestRes.has_value());
   REQUIRE(to_hex(out) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

   SECTION("Multiple writes") {
      sha.reset();
      sink.reset(0);
      const char *part1 = "abcdbcdecdefdefgefghfghighijhijk";
      const char *part2 = "ijkljklmklmnlmnomnopnopq";
      
      dw.write(std::span{ reinterpret_cast<const uint8_t *>(part1), strlen(part1) });
      dw.write(std::span{ reinterpret_cast<const uint8_t *>(part2), strlen(part2) });

      std::array<uint8_t, 32> out2{};
      auto digestRes2 = sha.getDigest(out2);
      REQUIRE(digestRes2.has_value());
      REQUIRE(to_hex(out2) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
      
      // Verify sink content
      REQUIRE(memcmp(sinkBuffer, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56) == 0);
   }
}

TEST_CASE("SHA256Digest with DigestReadable", "[sha256][streams]") {
   SHA256Digest sha;
   const char *msg = "abc";
   ArrayReadable source(std::span{ reinterpret_cast<const uint8_t *>(msg), 3 });
   DigestReadable dr(source, sha);

   uint8_t buffer[3];
   auto res = dr.read(std::span<uint8_t>(buffer, 3));
   REQUIRE(res.has_value());
   REQUIRE(res.value() == 3);
   REQUIRE(memcmp(buffer, msg, 3) == 0);

   // Check the digest
   std::array<uint8_t, 32> out{};
   auto digestRes = sha.getDigest(out);
   REQUIRE(digestRes.has_value());
   REQUIRE(to_hex(out) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
