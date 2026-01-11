#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <settings/Setting.h>
#include <settings/SettingsManager.h>
#include <streams/ArrayWritable.h>
#include <streams/StringReadable.h>

TEST_CASE("[Settings] storeSettings", "[Settings]") {
   Setting<int32_t> s1("Temperature", "Maximum", 150);
   Setting<const char *> s2("Device name", "uoc1");
   Setting<bool> s3("Working", true);
   uint8_t buffer[2000]{};
   ArrayWritable writable(buffer);
   auto res = SettingsManager::getInstance().save(writable);
   REQUIRE(res);
   std::cout << writable.written() << " bytes written: \n" << buffer;
   REQUIRE(writable.written() == 95);
}

TEST_CASE("[Settings] loadSettings", "[Settings]") {
   Setting<int32_t> s1("Temperature", "Maximum", 150);
   Setting<const char *> s2("Device name", "uoc1");
   Setting<bool> s3("Working", true);
   Setting<int32_t> s4("Another", 100);
   StringReadable readable("{\n"
                           "  \"Temperature\": {\n"
                           "    \"Maximum\": 250\n"
                           "  },\n"
                           "  \"Device name\": \"uoc2\",\n"
                           "  \"Working\": false\n"
                           "}");
   auto res = SettingsManager::getInstance().load(readable);
   REQUIRE(res);
   REQUIRE(s1.getValue() == 250);
   REQUIRE(s2.getValue() == std::string("uoc2"));
   REQUIRE(s3.getValue() == false);
   REQUIRE(s4.getValue() == 100);
}
