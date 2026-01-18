#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include <streams/ArrayWritable.h>
#include <streams/BufferedWritable.h>
#include <vector>

// Mock sink to track writes and flushes
class MockSink : public Writable {
   public:
   std::vector<uint8_t> recordedData;
   size_t flushCount = 0;
   std::optional<Error> writeError;
   std::optional<Error> flushError;

   std::expected<size_t, Error> write(std::span<const uint8_t> data) override {
      if(writeError) {
         return std::unexpected(*writeError);
      }
      recordedData.insert(recordedData.end(), data.begin(), data.end());
      return data.size();
   }

   std::expected<void, Error> flush() override {
      if(flushError) {
         return std::unexpected(*flushError);
      }
      flushCount++;
      return {};
   }
};

TEST_CASE("[BufferedWritable] basic buffering", "[BufferedWritable]") {
   MockSink sink;
   uint8_t buffer[4];
   BufferedWritable bw(sink, buffer);

   auto res = bw.write(std::span(reinterpret_cast<const uint8_t *>("abc"), 3));
   REQUIRE(res);
   REQUIRE(res.value() == 3);
   REQUIRE(sink.recordedData.empty()); // Should still be in buffer
   REQUIRE(bw.getBufferIndex() == 3);
}

TEST_CASE("[BufferedWritable] full buffer write", "[BufferedWritable]") {
   MockSink sink;
   uint8_t buffer[4];
   BufferedWritable bw(sink, buffer);

   bw.write(std::span(reinterpret_cast<const uint8_t *>("abcd"), 4));
   REQUIRE(sink.recordedData.size() == 4);
   REQUIRE(memcmp(sink.recordedData.data(), "abcd", 4) == 0);
   REQUIRE(bw.getBufferIndex() == 0);
}

TEST_CASE("[BufferedWritable] large write across buffer boundaries", "[BufferedWritable]") {
   MockSink sink;
   uint8_t buffer[4];
   BufferedWritable bw(sink, buffer);

   // Write 10 bytes: 4 + 4 + 2 remaining in buffer
   bw.write(std::span(reinterpret_cast<const uint8_t *>("0123456789"), 10));
   REQUIRE(sink.recordedData.size() == 8);
   REQUIRE(memcmp(sink.recordedData.data(), "01234567", 8) == 0);
   REQUIRE(bw.getBufferIndex() == 2);

   bw.flush();
   REQUIRE(sink.recordedData.size() == 10);
   REQUIRE(memcmp(sink.recordedData.data(), "0123456789", 10) == 0);
}

TEST_CASE("[BufferedWritable] partial write remains in buffer", "[BufferedWritable]") {
   MockSink sink;
   uint8_t buffer[10];
   BufferedWritable bw(sink, buffer);

   bw.write(std::span(reinterpret_cast<const uint8_t *>("12345"), 5));
   REQUIRE(sink.recordedData.empty());
   REQUIRE(bw.getBufferIndex() == 5);

   bw.write(std::span(reinterpret_cast<const uint8_t *>("67890"), 5));
   REQUIRE(sink.recordedData.size() == 10);
   REQUIRE(memcmp(sink.recordedData.data(), "1234567890", 10) == 0);
   REQUIRE(bw.getBufferIndex() == 0);
}

TEST_CASE("[BufferedWritable] flush behavior", "[BufferedWritable]") {
   MockSink sink;
   uint8_t buffer[4];
   BufferedWritable bw(sink, buffer);

   bw.write(std::span(reinterpret_cast<const uint8_t *>("xy"), 2));
   REQUIRE(sink.recordedData.empty());

   auto res = bw.flush();
   REQUIRE(res);
   REQUIRE(sink.recordedData.size() == 2);
   REQUIRE(sink.recordedData[0] == 'x');
   REQUIRE(sink.recordedData[1] == 'y');
   REQUIRE(sink.flushCount == 1);
   REQUIRE(bw.getBufferIndex() == 0);
}

TEST_CASE("[BufferedWritable] error propagation", "[BufferedWritable]") {
   MockSink sink;
   uint8_t buffer[4];
   BufferedWritable bw(sink, buffer);

   SECTION("write error") {
      sink.writeError = Writable::CouldNotWrite;
      auto res = bw.write(std::span(reinterpret_cast<const uint8_t *>("abcd"), 4));
      REQUIRE_FALSE(res);
      REQUIRE(res.error() == Writable::CouldNotWrite);
   }

   SECTION("flush error") {
      sink.flushError = Writable::CouldNotWrite;
      auto res = bw.flush();
      REQUIRE_FALSE(res);
      REQUIRE(res.error() == Writable::CouldNotWrite);
   }

   SECTION("partial write on flush") {
      class PartialSink : public Writable {
         public:
         std::expected<size_t, Error> write(std::span<const uint8_t> data) override {
            return data.size() / 2;
         }
         std::expected<void, Error> flush() override {
            return {};
         }
      } pSink;
      BufferedWritable bw2(pSink, buffer);
      bw2.write(std::span(reinterpret_cast<const uint8_t *>("ab"), 2));
      REQUIRE(bw2.getBufferIndex() == 2);
      auto res = bw2.flush();
      REQUIRE_FALSE(res);
      REQUIRE(bw2.getBufferIndex() == 1);
   }
}
