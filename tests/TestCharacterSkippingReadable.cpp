#include <optional>
#include <doctest/doctest.h>
#include <string_view>
#include <streams/ArrayWritable.h>
#include <streams/CharacterSkippingReadable.h>
#include <streams/StringReadable.h>
#include <utils/streamutils.h>

namespace TestCharacterSkippingReadable {
const char *testString = "A string to test Readable::readUntil";

TEST_CASE("[CharacterSkippingReadable] CharacterSkippingReadable skipSingleChar [Readable]") {
   StringReadable readable(testString);
   CharacterSkippingReadable csReadable(readable, "g");
   uint8_t buffer[100]{};
   ArrayWritable arrayWritable(buffer);
   REQUIRE(utils::copyStream(csReadable, arrayWritable) == strlen(testString) - 1);
   REQUIRE(std::string_view(reinterpret_cast<char *>(buffer)) == "A strin to test Readable::readUntil");
}

TEST_CASE("[CharacterSkippingReadable] CharacterSkippingReadable skipSingleChar2 [Readable]") {
   StringReadable readable(testString);
   CharacterSkippingReadable csReadable(readable, ":");
   uint8_t buffer[100]{};
   ArrayWritable arrayWritable(buffer);
   REQUIRE(utils::copyStream(csReadable, arrayWritable) == strlen(testString) - 2);
   REQUIRE(std::string_view(reinterpret_cast<char *>(buffer)) == "A string to test ReadablereadUntil");
}

TEST_CASE("[CharacterSkippingReadable] CharacterSkippingReadable skipMultipleChars [Readable]") {
   StringReadable readable(testString);
   CharacterSkippingReadable csReadable(readable, ":e");
   uint8_t buffer[100]{};
   ArrayWritable arrayWritable(buffer);
   REQUIRE(utils::copyStream(csReadable, arrayWritable) == strlen(testString) - 6);
   REQUIRE(std::string_view(reinterpret_cast<char *>(buffer)) == "A string to tst RadablradUntil");
}

TEST_CASE("[CharacterSkippingReadable] CharacterSkippingReadable skipFinalChars [Readable]") {
   StringReadable readable(testString);
   CharacterSkippingReadable csReadable(readable, "il");
   uint8_t buffer[100]{};
   ArrayWritable arrayWritable(buffer);
   REQUIRE(utils::copyStream(csReadable, arrayWritable) == strlen(testString) - 4);
   REQUIRE(std::string_view(reinterpret_cast<char *>(buffer)) == "A strng to test Readabe::readUnt");
}


} // namespace TestCharacterSkippingReadable