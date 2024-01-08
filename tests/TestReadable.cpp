#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <streams/ArrayReadable.h>
#include <streams/StringReadable.h>

const char *testString = "A string to test Readable::readUntil";
const char *testScanf = "Parse the number 334 or hex 0x15e2 binary 0b1110101 and -657";


TEST_CASE("[Readable] ArrayReadable bytesLeft", "[Readable]") {
   StringReadable readable(testString);
   REQUIRE(readable.bytesLeft() == strlen(testString));
}

TEST_CASE("[Readable] Read beyond the end", "[Readable]") {
   StringReadable readable(testString);
   auto res = readable.read();
   while (res) {
      res = readable.read();
   }
   REQUIRE(res.error() == Readable::EndOfStream);
}


TEST_CASE("[Readable] String match", "[Readable]") {
   StringReadable readable(testString);
   // Test string match find
   REQUIRE(readable.readUntil({ "test" }) == 0);
   readable.reset();
   size_t nread;
   // Test string match position
   REQUIRE(readable.readUntil({ "test" }, std::span<char>(), &nread) == 0);
   REQUIRE(nread == 16);
}

TEST_CASE("[Readable] String match repeated character", "[Readable]") {
   StringReadable readable(testString);
   size_t nread;
   REQUIRE(readable.readUntil({ ":read" }, std::span<char>(), &nread) == 0);
   REQUIRE(nread == 31);
}

TEST_CASE("[Readable] readUntil match with buffer", "[Readable]") {
   StringReadable readable(testString);
   size_t nread;
   char buffer[32]{};
   REQUIRE(readable.readUntil({ ":read" }, buffer, &nread) == 0);
   REQUIRE(nread == 31);
   REQUIRE(std::string(buffer) == "A string to test Readable::read");
}

TEST_CASE("[Readable] readUntil multiple match", "[Readable]") {
   StringReadable readable(testString);
   size_t nread;
   REQUIRE(readable.readUntil({ "test", ":read" }, std::span<char>(), &nread) == 0);
   REQUIRE(nread == 16);
}

TEST_CASE("[Readable] readUntil multiple match 2", "[Readable]") {
   StringReadable readable(testString);
   size_t nread;
   REQUIRE(readable.readUntil({ ":read", "test" }, std::span<char>(), &nread) == 1);
   REQUIRE(nread == 16);
}

TEST_CASE("[Readable] scanf", "[Readable]") {
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

TEST_CASE("[Readable] scanf skipped", "[Readable]") {
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

TEST_CASE("[Readable] scanf not found", "[Readable]") {
   StringReadable readable(testScanf);
   uint32_t unum = 0;
   auto count = readable.scanf("Parse the %u ", &unum);
   REQUIRE(count == 0);
   REQUIRE(unum == 0);
}

TEST_CASE("[Readable] scanf not found 2", "[Readable]") {
   StringReadable readable(testScanf);
   uint32_t unum = 0;
   uint32_t unum2 = 0;
   auto count = readable.scanf("Parse the number %u %u", &unum, &unum2);
   REQUIRE(count == 1);
   REQUIRE(unum == 334);
   REQUIRE(unum2 == 0);
}

TEST_CASE("[Readable] scanf chars limit", "[Readable]") {
   StringReadable readable("testScanf -4123456 4123456");
   int32_t num = 0;
   uint32_t unum = 0;
   auto count = readable.scanf("testScanf %5d%*s %5u", &num, &unum);
   REQUIRE(count == 2);
   REQUIRE(num == -4123);
   REQUIRE(unum == 41234);
}

TEST_CASE("[Readable] scanf chars limit 2", "[Readable]") {
   StringReadable readable("testScanf -4123456 4123456");
   uint32_t unum = 0;
   char buffer[10];
   auto count = readable.scanf("testScanf %5s%*s %5u", buffer, &unum);
   REQUIRE(count == 2);
   REQUIRE(std::string(buffer) == "-4123");
   REQUIRE(unum == 41234);
}
