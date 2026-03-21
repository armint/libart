
#include <doctest/doctest.h>
#include <cstring>
#include <string>

#include <logging/Level.h>
#include <logging/LogManager.h>
#include <logging/Logger.h>
#include <streams/ArrayWritable.h>

// --- levelFromString tests ---

TEST_CASE("[Logging] levelFromString parses valid level strings") {
   CHECK(logging::levelFromString("Trace") == logging::Level::Trace);
   CHECK(logging::levelFromString("Debug") == logging::Level::Debug);
   CHECK(logging::levelFromString("Info") == logging::Level::Info);
   CHECK(logging::levelFromString("Warning") == logging::Level::Warning);
   CHECK(logging::levelFromString("Error") == logging::Level::Error);
}

TEST_CASE("[Logging] levelFromString returns nullopt for invalid strings") {
   CHECK(!logging::levelFromString("trace"));
   CHECK(!logging::levelFromString("TRACE"));
   CHECK(!logging::levelFromString("info"));
   CHECK(!logging::levelFromString(""));
   CHECK(!logging::levelFromString("Unknown"));
   CHECK(!logging::levelFromString(nullptr));
}

// --- Logger registry tests ---

TEST_CASE("[Logging] Logger auto-registers with LogManager") {
   logging::Logger logger("TestTopic");
   auto *found = LogManager::getInstance().findLogger("TestTopic");
   REQUIRE(found == &logger);
}

TEST_CASE("[Logging] findLogger returns nullptr for unknown topic") {
   auto *found = LogManager::getInstance().findLogger("NonExistentTopic12345");
   REQUIRE(found == nullptr);
}

TEST_CASE("[Logging] findLogger returns nullptr for null topic") {
   auto *found = LogManager::getInstance().findLogger(nullptr);
   REQUIRE(found == nullptr);
}

TEST_CASE("[Logging] Multiple loggers can be found by topic") {
   logging::Logger loggerA("TopicAlpha");
   logging::Logger loggerB("TopicBeta");

   CHECK(LogManager::getInstance().findLogger("TopicAlpha") == &loggerA);
   CHECK(LogManager::getInstance().findLogger("TopicBeta") == &loggerB);
}

// --- Logger level control ---

TEST_CASE("[Logging] Logger default level is Info") {
   logging::Logger logger("LevelTest");
   REQUIRE(logger.level() == logging::Level::Info);
}

TEST_CASE("[Logging] Logger set_level changes level") {
   logging::Logger logger("LevelTest2");
   logger.set_level(logging::Level::Trace);
   CHECK(logger.level() == logging::Level::Trace);

   logger.set_level(logging::Level::Error);
   CHECK(logger.level() == logging::Level::Error);
}

TEST_CASE("[Logging] Logger isEnabled respects logger level") {
   logging::Logger logger("EnabledTest");
   LogManager::getInstance().set_global_level(logging::Level::Trace);

   logger.set_level(logging::Level::Warning);
   CHECK(!logger.isEnabled(logging::Level::Trace));
   CHECK(!logger.isEnabled(logging::Level::Debug));
   CHECK(!logger.isEnabled(logging::Level::Info));
   CHECK(logger.isEnabled(logging::Level::Warning));
   CHECK(logger.isEnabled(logging::Level::Error));
}

TEST_CASE("[Logging] Logger isEnabled respects global level") {
   logging::Logger logger("GlobalTest");
   logger.set_level(logging::Level::Trace);

   LogManager::getInstance().set_global_level(logging::Level::Warning);
   CHECK(!logger.isEnabled(logging::Level::Trace));
   CHECK(!logger.isEnabled(logging::Level::Debug));
   CHECK(!logger.isEnabled(logging::Level::Info));
   CHECK(logger.isEnabled(logging::Level::Warning));
   CHECK(logger.isEnabled(logging::Level::Error));

   // Reset global level
   LogManager::getInstance().set_global_level(logging::Level::Trace);
}

TEST_CASE("[Logging] Logger topic accessor returns topic") {
   logging::Logger logger("MyTopic");
   REQUIRE(std::strcmp(logger.topic(), "MyTopic") == 0);
}

// --- Logger output ---

TEST_CASE("[Logging] Logger writes formatted output to sink") {
   uint8_t buffer[256] = {};
   ArrayWritable writable(buffer);
   LogManager::getInstance().set_sink(&writable);
   LogManager::getInstance().set_formatter(nullptr);
   LogManager::getInstance().set_global_level(logging::Level::Trace);

   logging::Logger logger("Out");
   logger.set_level(logging::Level::Trace);
   logger.info("hello %d", 42);

   std::string output(reinterpret_cast<char *>(buffer), writable.written());
   CHECK(output.find("[Out]") != std::string::npos);
   CHECK(output.find("[INFO]") != std::string::npos);
   CHECK(output.find("hello 42") != std::string::npos);

   LogManager::getInstance().set_sink(nullptr);
}

TEST_CASE("[Logging] Logger does not write when level is disabled") {
   uint8_t buffer[256] = {};
   ArrayWritable writable(buffer);
   LogManager::getInstance().set_sink(&writable);
   LogManager::getInstance().set_formatter(nullptr);
   LogManager::getInstance().set_global_level(logging::Level::Trace);

   logging::Logger logger("Silent");
   logger.set_level(logging::Level::Error);
   logger.info("should not appear");

   REQUIRE(writable.written() == 0);

   LogManager::getInstance().set_sink(nullptr);
}
