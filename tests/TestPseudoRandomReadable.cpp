#include <doctest/doctest.h>
#include <streams/PseudoRandomReadable.h>
#include <vector>

TEST_CASE("[PseudoRandomReadable] Determinism [PseudoRandomReadable]") {
   PseudoRandomReadable prr1(12345);
   PseudoRandomReadable prr2(12345);

   for (int i = 0; i < 100; ++i) {
      auto val1 = prr1.read();
      auto val2 = prr2.read();
      REQUIRE(val1.has_value());
      REQUIRE(val2.has_value());
      REQUIRE(val1.value() == val2.value());
   }
}

TEST_CASE("[PseudoRandomReadable] Different seeds [PseudoRandomReadable]") {
   PseudoRandomReadable prr1(12345);
   PseudoRandomReadable prr2(54321);

   bool different = false;
   for (int i = 0; i < 100; ++i) {
      auto val1 = prr1.read();
      auto val2 = prr2.read();
      REQUIRE(val1.has_value());
      REQUIRE(val2.has_value());
      if (val1.value() != val2.value()) {
         different = true;
         break;
      }
   }
   REQUIRE(different);
}

TEST_CASE("[PseudoRandomReadable] available() is always true [PseudoRandomReadable]") {
   PseudoRandomReadable prr(12345);
   REQUIRE(prr.available() == true);
   for (int i = 0; i < 100; ++i) {
      prr.read();
      REQUIRE(prr.available() == true);
   }
}

TEST_CASE("[PseudoRandomReadable] Sequence consistency [PseudoRandomReadable]") {
   PseudoRandomReadable prr(0xDEADBEEF);
   std::vector<uint8_t> bytes;
   for (int i = 0; i < 8; ++i) {
      bytes.push_back(static_cast<uint8_t>(prr.read().value()));
   }

   // Resetting with same seed should produce same bytes
   PseudoRandomReadable prr2(0xDEADBEEF);
   for (int i = 0; i < 8; ++i) {
      REQUIRE(static_cast<uint8_t>(prr2.read().value()) == bytes[i]);
   }
}

TEST_CASE("[PseudoRandomReadable] Byte order [PseudoRandomReadable]") {
   PseudoRandomReadable prr(12345);
   pcg::pcg32 rng(12345, 1);
   
   for (int i = 0; i < 10; ++i) {
      uint32_t expected_next = rng.next();
      for (int j = 0; j < 4; ++j) {
         auto val = prr.read();
         REQUIRE(val.has_value());
         REQUIRE(static_cast<uint8_t>(val.value()) == static_cast<uint8_t>(expected_next & 0xFF));
         expected_next >>= 8;
      }
   }
}
