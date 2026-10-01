#include "utils/compat/span.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <etl/vector.h>
#include <optional>

#include "utils/IBMCRC16Digest.h"
#include "utils/IBMCRC16FastDigest.h"
#include "utils/CrcDigest.h"

static uint16_t digest_value(Digest &d) {
   std::array<uint8_t, sizeof(uint16_t)> out{};
   auto res = d.getDigest(out);
   REQUIRE(res.has_value());
   REQUIRE(res.value() == sizeof(uint16_t));
   return *reinterpret_cast<uint16_t *>(out.data());
}


TEST_CASE("ModbusCrc from new implementation matches IBMCRC16Digest [crc16]") {
   ModbusCrc modbus{};
   IBMCRC16Digest slow{};

   const char *msg = "123456789";
   art::span<const uint8_t> data{ reinterpret_cast<const uint8_t *>(msg), strlen(msg) };
   modbus.update(data);
   slow.update(data);

   REQUIRE(digest_value(slow) == 0x4B37);
   REQUIRE(digest_value(modbus) == digest_value(slow));

   SUBCASE("Binary pattern with zeros") {
      const std::array<uint8_t, 5> data{ 0x00, 0x01, 0x02, 0xFF, 0xFE };
      modbus.update(data);
      slow.update(data);
      REQUIRE(digest_value(modbus) == digest_value(slow));
   }

   SUBCASE("Large random-ish buffer (chunked updates)") {
      etl::vector<uint8_t, 256> data;
      for(int i = 0; i < 256; ++i) {
         data.push_back(static_cast<uint8_t>((i * 17 + 3) ^ (i >> 2)));
      }

      // Monolithic
      ModbusCrc modbus_mono{};
      IBMCRC16Digest slow_mono{};
      modbus_mono.update(data);
      slow_mono.update(data);

      // Chunked
      ModbusCrc modbus_chunk{};
      IBMCRC16Digest slow_chunk{};

      size_t chunk_size = 0;
      for (size_t i = 0; i < data.size(); ++i) {
         if (i % 17 == 0) ++chunk_size;
         art::span<const uint8_t> part{ &data[i], 1 };
         modbus_chunk.update(part);
         slow_chunk.update(part);
      }

      auto m_mono = digest_value(modbus_mono);
      auto s_mono = digest_value(slow_mono);
      auto m_chunk = digest_value(modbus_chunk);
      auto s_chunk = digest_value(slow_chunk);

      REQUIRE(m_mono == s_mono);
      REQUIRE(m_chunk == s_chunk);
      REQUIRE(m_mono == m_chunk);
   }
}

TEST_CASE("IBMCRC16Digest and IBMCRC16FastDigest produce identical digests (basic cases) [crc16]") {
   IBMCRC16Digest slow{};     // default: polynomial 0xA001, init 0xFFFF
   IBMCRC16FastDigest fast{}; // default: init 0xFFFF

   SUBCASE("Empty input") {
      REQUIRE(digest_value(slow) == digest_value(fast));
   }

   SUBCASE("Known short message: 123456789") {
      const char *msg = "123456789";
      art::span<const uint8_t> data{ reinterpret_cast<const uint8_t *>(msg), strlen(msg) };
      slow.update(data);
      fast.update(data);
      auto s = digest_value(slow);
      auto f = digest_value(fast);
      REQUIRE(s == f);
   }

   SUBCASE("Binary data with zeros and high bits") {
      const std::array<uint8_t, 10> data{ 0x00, 0xFF, 0x01, 0x80, 0x7F, 0x55, 0xAA, 0x10, 0x20, 0x30 };
      slow.update(data);
      fast.update(data);
      REQUIRE(digest_value(slow) == digest_value(fast));
   }
}

TEST_CASE("IBMCRC16Digest and IBMCRC16FastDigest produce identical digests (chunked updates) [crc16]") {
   IBMCRC16Digest slow{};
   IBMCRC16FastDigest fast{};

   // Prepare a moderately sized buffer with varying values
   etl::vector<uint8_t, 256> data;
   for(int i = 0; i < 256; ++i) {
      data.push_back(static_cast<uint8_t>((i * 37) ^ (i >> 1))); // pseudo-random-ish pattern
   }

   // Monolithic update for reference
   IBMCRC16Digest slow_mono{};
   IBMCRC16FastDigest fast_mono{};
   slow_mono.update(data);
   fast_mono.update(data);

   // Chunked updates with varying sizes
   size_t pos = 0;
   size_t chunk = 1;
   while(pos < data.size()) {
      size_t remaining = data.size() - pos;
      size_t take = std::min(remaining, chunk);
      art::span<const uint8_t> part{ data.data() + pos, take };
      slow.update(part);
      fast.update(part);
      pos += take;
      chunk = (chunk % 17) + 1; // vary chunk size from 1..17
   }

   // Compare to each other and to monolithic
   auto s_chunked = digest_value(slow);
   auto f_chunked = digest_value(fast);
   auto s_mono = digest_value(slow_mono);
   auto f_mono = digest_value(fast_mono);

   REQUIRE(s_chunked == f_chunked);
   REQUIRE(s_chunked == s_mono);
   REQUIRE(f_chunked == f_mono);
}