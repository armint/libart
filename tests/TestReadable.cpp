#include <string>
#include <optional>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <doctest/doctest.h>
#include <streams/ArrayReadable.h>
#include <streams/Readable.h>
#include <streams/StringReadable.h>
#include <utils/endian.h>

const char *testString = "A string to test Readable::readUntil";
const char *testScanf = "Parse the number 334 or hex 0x15e2 binary 0b1110101 and -657";


TEST_CASE("[Readable] ArrayReadable bytesLeft [Readable]") {
   StringReadable readable(testString);
   REQUIRE(readable.bytesLeft() == strlen(testString));
}

TEST_CASE("[Readable] Read beyond the end [Readable]") {
   StringReadable readable(testString);
   auto res = readable.read();
   while(res) {
      res = readable.read();
   }
   REQUIRE(res.error() == Readable::EndOfStream);
}


TEST_CASE("[Readable] String match [Readable]") {
   StringReadable readable(testString);
   // Test string match find
   REQUIRE(readable.readUntil({ "test" }) == 0);
   readable.reset();
   size_t nread;
   // Test string match position
   REQUIRE(readable.readUntil({ "test" }, std::span<char>(), &nread) == 0);
   REQUIRE(nread == 16);
}

TEST_CASE("[Readable] String match repeated character [Readable]") {
   StringReadable readable(testString);
   size_t nread;
   REQUIRE(readable.readUntil({ ":read" }, std::span<char>(), &nread) == 0);
   REQUIRE(nread == 31);
}

TEST_CASE("[Readable] readUntil match with buffer [Readable]") {
   StringReadable readable(testString);
   size_t nread;
   char buffer[32]{};
   REQUIRE(readable.readUntil({ ":read" }, buffer, &nread) == 0);
   REQUIRE(nread == 31);
   REQUIRE(std::string(buffer) == "A string to test Readable::read");
}

TEST_CASE("[Readable] readUntil multiple match [Readable]") {
   StringReadable readable(testString);
   size_t nread;
   REQUIRE(readable.readUntil({ "test", ":read" }, std::span<char>(), &nread) == 0);
   REQUIRE(nread == 16);
}

TEST_CASE("[Readable] readUntil multiple match 2 [Readable]") {
   StringReadable readable(testString);
   size_t nread;
   REQUIRE(readable.readUntil({ ":read", "test" }, std::span<char>(), &nread) == 1);
   REQUIRE(nread == 16);
}

TEST_CASE("[Readable] scanf [Readable]") {
   StringReadable readable(testScanf);
   uint32_t unum;
   uint32_t hexnum;
   uint32_t bnum;
   int32_t snum;
   char buffer[10];
   auto count = readable.scanf("Parse the number %u or %s 0x%x binary 0b%b and %d", &unum, buffer, &hexnum, &bnum, &snum);
   REQUIRE(count == 5);
   REQUIRE(unum == 334);
   REQUIRE(hexnum == 0x15e2);
   REQUIRE(std::string(buffer) == "hex");
   REQUIRE(bnum == 0b1110101);
   REQUIRE(snum == -657);
}

TEST_CASE("[Readable] scanf skipped [Readable]") {
   StringReadable readable(testScanf);
   uint32_t unum;
   uint32_t bnum;
   int32_t snum;
   char buffer[10];
   auto count = readable.scanf("Parse the number %u or %s 0x%*x binary 0b%b and %d", &unum, buffer, &bnum, &snum);
   REQUIRE(count == 4);
   REQUIRE(unum == 334);
   REQUIRE(std::string(buffer) == "hex");
   REQUIRE(bnum == 0b1110101);
   REQUIRE(snum == -657);
}

TEST_CASE("[Readable] scanf not found [Readable]") {
   StringReadable readable(testScanf);
   uint32_t unum = 0;
   auto count = readable.scanf("Parse the %u ", &unum);
   REQUIRE(count == 0);
   REQUIRE(unum == 0);
}

TEST_CASE("[Readable] scanf not found 2 [Readable]") {
   StringReadable readable(testScanf);
   uint32_t unum = 0;
   uint32_t unum2 = 0;
   auto count = readable.scanf("Parse the number %u %u", &unum, &unum2);
   REQUIRE(count == 1);
   REQUIRE(unum == 334);
   REQUIRE(unum2 == 0);
}

TEST_CASE("[Readable] scanf chars limit [Readable]") {
   StringReadable readable("testScanf -4123456 4123456");
   int32_t num = 0;
   uint32_t unum = 0;
   auto count = readable.scanf("testScanf %5d%*s %5u", &num, &unum);
   REQUIRE(count == 2);
   REQUIRE(num == -4123);
   REQUIRE(unum == 41234);
}

TEST_CASE("[Readable] scanf chars limit 2 [Readable]") {
   StringReadable readable("testScanf -4123456 4123456");
   uint32_t unum = 0;
   char buffer[10];
   auto count = readable.scanf("testScanf %5s%*s %5u", buffer, &unum);
   REQUIRE(count == 2);
   REQUIRE(std::string(buffer) == "-4123");
   REQUIRE(unum == 41234);
}

TEST_CASE("[Readable] read(span<uint8_t>) basics [Readable]") {
   const std::array<uint8_t, 4> data{ 1, 2, 3, 4 };
   ArrayReadable readable(std::span<const uint8_t>(data.data(), data.size()));
   std::array<uint8_t, 4> out{};
   Readable &base = readable;
   auto r = base.read(std::span<uint8_t>(out.data(), out.size()));
   REQUIRE(r);
   REQUIRE(r.value() == out.size());
   REQUIRE(out == data);
   // Next read should hit end-of-stream
   std::array<uint8_t, 1> one{};
   auto r2 = base.read(std::span<uint8_t>(one.data(), one.size()));
   REQUIRE(!r2);
   REQUIRE(r2.error() == Readable::EndOfStream);
}

TEST_CASE("[Readable] readNumber endianness [Readable]") {
   // Test 16-bit value little endian
   const uint16_t val16 = 0x1234;
   const std::array<uint8_t, 2> le16{ 0x34, 0x12 };
   ArrayReadable r16le(std::span<const uint8_t>(le16.data(), le16.size()));
   auto v16le = r16le.readNumber<uint16_t, Endian::Little>();
   REQUIRE(v16le);
   REQUIRE(v16le.value() == val16);

   // Test 16-bit big endian
   const std::array<uint8_t, 2> be16{ 0x12, 0x34 };
   ArrayReadable r16be(std::span<const uint8_t>(be16.data(), be16.size()));
   auto v16be = r16be.readNumber<uint16_t, Endian::Big>();
   REQUIRE(v16be);
   REQUIRE(v16be.value() == val16);

   // Test 32-bit little endian
   const uint32_t val32 = 0x0A1B2C3D;
   const std::array<uint8_t, 4> le32{ 0x3D, 0x2C, 0x1B, 0x0A };
   ArrayReadable r32le(std::span<const uint8_t>(le32.data(), le32.size()));
   auto v32le = r32le.readNumber<uint32_t, Endian::Little>();
   REQUIRE(v32le);
   REQUIRE(v32le.value() == val32);

   // Test 32-bit big endian
   const std::array<uint8_t, 4> be32{ 0x0A, 0x1B, 0x2C, 0x3D };
   ArrayReadable r32be(std::span<const uint8_t>(be32.data(), be32.size()));
   auto v32be = r32be.readNumber<uint32_t, Endian::Big>();
   REQUIRE(v32be);
   REQUIRE(v32be.value() == val32);

   // Error propagation when not enough bytes
   const std::array<uint8_t, 1> shortBuf{ 0xFF };
   ArrayReadable rShort(std::span<const uint8_t>(shortBuf.data(), shortBuf.size()));
   auto vErr = rShort.readNumber<uint16_t, Endian::Little>();
   REQUIRE(!vErr);
   REQUIRE(vErr.error() == Readable::EndOfStream);
}

TEST_CASE("[Readable] readRLE decoding and overflow [Readable]") {
   // 300 encoded as LEB128: 0xAC 0x02
   const std::array<uint8_t, 2> enc300{ 0xAC, 0x02 };
   ArrayReadable r300(std::span<const uint8_t>(enc300.data(), enc300.size()));
   uint32_t dest = 0;
   auto consumed = r300.readRLE<uint32_t>(dest);
   REQUIRE(consumed);
   REQUIRE(consumed.value() == 2);
   REQUIRE(dest == 300);

   // Overflow for uint8_t: require too many continuation groups
   const std::array<uint8_t, 3> overflow{ 0x80, 0x80, 0x00 };
   ArrayReadable rOv(std::span<const uint8_t>(overflow.data(), overflow.size()));
   uint8_t d8 = 0;
   auto consumedOv = rOv.readRLE<uint8_t>(d8);
   REQUIRE(!consumedOv);
   REQUIRE(consumedOv.error() == Readable::NumberTooBig);
}

TEST_CASE("[Readable] timeout accessors [Readable]") {
   StringReadable readable("abc");
   REQUIRE(readable.getTimeout() == 0);
   readable.setTimeout(1234);
   REQUIRE(readable.getTimeout() == 1234);
}
