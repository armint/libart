
#include "utils/compat/span.h"
#include <cstring>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

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

// A Logger is linked into the LogManager list by address, so it cannot be copied or moved.
static_assert(!std::is_copy_constructible_v<logging::Logger>);
static_assert(!std::is_copy_assignable_v<logging::Logger>);
static_assert(!std::is_move_constructible_v<logging::Logger>);
static_assert(!std::is_move_assignable_v<logging::Logger>);

// A destroyed Logger must leave the LogManager list (#37). std::optional controls the lifetimes without the heap;
// its storage outlives the Logger in it, so a dangling entry would be found again instead of crashing.
TEST_CASE("[Logging] Destroyed logger is no longer found") {
   std::optional<logging::Logger> first;
   std::optional<logging::Logger> middle;
   std::optional<logging::Logger> last;
   first.emplace("DestroyFirst");
   middle.emplace("DestroyMiddle");
   last.emplace("DestroyLast");

   middle.reset();
   CHECK(LogManager::getInstance().findLogger("DestroyMiddle") == nullptr);
   CHECK(LogManager::getInstance().findLogger("DestroyFirst") == &*first);
   CHECK(LogManager::getInstance().findLogger("DestroyLast") == &*last);

   // The most recently registered Logger is the head of the list.
   last.reset();
   CHECK(LogManager::getInstance().findLogger("DestroyLast") == nullptr);
   CHECK(LogManager::getInstance().findLogger("DestroyFirst") == &*first);

   first.reset();
   CHECK(LogManager::getInstance().findLogger("DestroyFirst") == nullptr);
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

// A Writable that records write session begin/end calls and the begin/end counts observed
// at the time of each write(), used to verify a Logger line is written inside one session.
class SessionCountingWritable : public Writable {
   public:
   explicit SessionCountingWritable(art::span<uint8_t> buffer)
      : _buffer(buffer) {}

   art::expected<size_t, Error> write(art::span<const uint8_t> data) override {
      // Record the session nesting depth open at the time of this write(): 0 = no session,
      // 1 = only the caller's own (e.g. printf()'s) session, 2 = also nested inside a wider
      // one held by a caller such as Logger. tfp_format() writes one character (a 1-byte
      // span) at a time via printf(); println()'s trailing CRLF is a direct 2-byte write().
      int depth = beginCount - endCount;
      if(data.size() == 1) {
         depthDuringCharacterWrite.push_back(depth);
      } else {
         depthDuringDirectWrite.push_back(depth);
      }
      size_t written = 0;
      for(size_t i = 0; i < data.size() && _position < _buffer.size(); ++i, ++_position, ++written) {
         _buffer[_position] = data[i];
      }
      if(written != data.size()) {
         return art::unexpected(Overflow);
      }
      return written;
   }

   art::expected<void, Error> flush() override {
      depthDuringFlush = beginCount - endCount;
      return {};
   }

   size_t written() const {
      return _position;
   }

   int beginCount = 0;
   int endCount = 0;
   int depthDuringFlush = 0;
   std::vector<int> depthDuringCharacterWrite;
   std::vector<int> depthDuringDirectWrite;

   protected:
   void beginWriteSession() noexcept override {
      ++beginCount;
   }
   void endWriteSession() noexcept override {
      ++endCount;
   }

   private:
   art::span<uint8_t> _buffer;
   size_t _position{ 0 };
};

TEST_CASE("[Logging] Logger holds a single write session across prefix, body, suffix and flush") {
   uint8_t buffer[256] = {};
   SessionCountingWritable writable(buffer);
   LogManager::getInstance().set_sink(&writable);
   LogManager::getInstance().set_formatter(nullptr);
   LogManager::getInstance().set_global_level(logging::Level::Trace);

   logging::Logger logger("SessionTest");
   logger.set_level(logging::Level::Trace);
   logger.info("value=%d", 7);

   // The outer session (plus one nested session per printf() call within it) was opened
   // and fully closed again by the time the line is done.
   CHECK(writable.beginCount > 0);
   CHECK(writable.beginCount == writable.endCount);

   // Characters written by printf() (the "[SessionTest][INFO] " prefix and the "value=7"
   // body) happen at depth 2: Logger's outer session plus vprintf()'s own inner one.
   REQUIRE_FALSE(writable.depthDuringCharacterWrite.empty());
   for(int depth : writable.depthDuringCharacterWrite) {
      CHECK(depth == 2);
   }
   // The CRLF written directly by println() (not through printf()) and flush() are not
   // wrapped in their own session, yet still happen at depth 1 -- proving they run inside
   // the wider session Logger holds across the whole line, not just inside a printf() call.
   REQUIRE(writable.depthDuringDirectWrite.size() == 1);
   CHECK(writable.depthDuringDirectWrite[0] == 1);
   CHECK(writable.depthDuringFlush == 1);

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

// --- LOG_* macro tests ---

// The macros take the format string as part of their variadic arguments, so a call without format arguments is
// valid ISO C++17. This file is also built into libArT-compat-tests-cpp17 with -Wpedantic -Werror, where the former
// ", ##__VA_ARGS__" form failed with GCC (C++17) and Clang (any standard).
TEST_CASE("[Logging] LOG_* macros accept a format string without arguments") {
   uint8_t buffer[256] = {};
   ArrayWritable writable(buffer);
   LogManager::getInstance().set_sink(&writable);
   LogManager::getInstance().set_formatter(nullptr);
   LogManager::getInstance().set_global_level(logging::Level::Trace);

   logging::Logger logger("Macro");
   logger.set_level(logging::Level::Trace);
   LOG_TRACE(logger, "trace text");
   LOG_DEBUG(logger, "debug text");
   LOG_INFO(logger, "info text");
   LOG_WARN(logger, "warn text");
   LOG_ERROR(logger, "error text");

   std::string output(reinterpret_cast<char *>(buffer), writable.written());
   CHECK(output.find("[Macro][TRACE] trace text") != std::string::npos);
   CHECK(output.find("[Macro][DEBUG] debug text") != std::string::npos);
   CHECK(output.find("[Macro][INFO] info text") != std::string::npos);
   CHECK(output.find("[Macro][WARN] warn text") != std::string::npos);
   CHECK(output.find("[Macro][ERROR] error text") != std::string::npos);

   LogManager::getInstance().set_sink(nullptr);
}

TEST_CASE("[Logging] LOG_* macros pass format arguments") {
   uint8_t buffer[256] = {};
   ArrayWritable writable(buffer);
   LogManager::getInstance().set_sink(&writable);
   LogManager::getInstance().set_formatter(nullptr);
   LogManager::getInstance().set_global_level(logging::Level::Trace);

   logging::Logger logger("Macro");
   logger.set_level(logging::Level::Trace);
   LOG_INFO(logger, "%s=%d", "value", 42);

   std::string output(reinterpret_cast<char *>(buffer), writable.written());
   CHECK(output.find("[Macro][INFO] value=42") != std::string::npos);

   LogManager::getInstance().set_sink(nullptr);
}

namespace {
int countedArgument(int &evaluations) {
   ++evaluations;
   return evaluations;
}
} // namespace

TEST_CASE("[Logging] LOG_* macros do not evaluate arguments of a disabled level") {
   uint8_t buffer[256] = {};
   ArrayWritable writable(buffer);
   LogManager::getInstance().set_sink(&writable);
   LogManager::getInstance().set_formatter(nullptr);
   LogManager::getInstance().set_global_level(logging::Level::Trace);

   logging::Logger logger("Macro");
   logger.set_level(logging::Level::Error);
   int evaluations = 0;
   LOG_WARN(logger, "%d", countedArgument(evaluations));
   CHECK(evaluations == 0);
   CHECK(writable.written() == 0);

   LOG_ERROR(logger, "%d", countedArgument(evaluations));
   CHECK(evaluations == 1);

   LogManager::getInstance().set_sink(nullptr);
}

TEST_CASE("[Logging] LOG_* macros below LOG_COMPILED_MIN_LEVEL do not evaluate arguments") {
   uint8_t buffer[256] = {};
   ArrayWritable writable(buffer);
   LogManager::getInstance().set_sink(&writable);
   LogManager::getInstance().set_formatter(nullptr);
   LogManager::getInstance().set_global_level(logging::Level::Trace);

   logging::Logger logger("Macro");
   logger.set_level(logging::Level::Trace);
   int evaluations = 0;
   // The macros read LOG_COMPILED_MIN_LEVEL where they are expanded, so redefining it here filters these calls at
   // compile time, the same as -DLOG_COMPILED_MIN_LEVEL=logging::Level::Error does for a whole build.
#pragma push_macro("LOG_COMPILED_MIN_LEVEL")
#undef LOG_COMPILED_MIN_LEVEL
#define LOG_COMPILED_MIN_LEVEL logging::Level::Error
   LOG_WARN(logger, "%d", countedArgument(evaluations));
   LOG_WARN(logger, "filtered");
   CHECK(evaluations == 0);
   CHECK(writable.written() == 0);

   LOG_ERROR(logger, "%d", countedArgument(evaluations));
   CHECK(evaluations == 1);
#pragma pop_macro("LOG_COMPILED_MIN_LEVEL")

   LogManager::getInstance().set_sink(nullptr);
}
