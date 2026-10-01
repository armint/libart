#include "FatFileMockGuard.h"
#include "storage/FatFile.h"
#include "streams/FatFileReadable.h"
#include <doctest/doctest.h>
#include <vector>


TEST_CASE("[FatFileReadable] basic reading [FatFileReadable]") {
   FatFileMockGuard guard;
   auto &mockState = guard.state;
   mockState.fileData = { 'a', 'b', 'c', 'd', 'e' };

   FatFile file("test.txt", 0);
   uint8_t bufferMem[3];
   FatFileReadable readable(file, bufferMem);

   SUBCASE("Read characters one by one") {
      auto res1 = readable.read();
      REQUIRE(res1.has_value());
      CHECK(*res1 == 'a');
      CHECK(readable.position() == 1);

      auto res2 = readable.read();
      REQUIRE(res2.has_value());
      CHECK(*res2 == 'b');
      CHECK(readable.position() == 2);
   }

   SUBCASE("Buffering works") {
      // First read should fill buffer with 'a', 'b', 'c'
      auto res1 = readable.read();
      REQUIRE(res1.has_value());
      CHECK(*res1 == 'a');

      // File position should be at 3 because 3 bytes were read into buffer
      CHECK(file.tell() == 3);
      // Readable position should be at 1
      CHECK(readable.position() == 1);

      readable.read(); // 'b'
      readable.read(); // 'c'

      // Next read should trigger another file read
      auto res4 = readable.read();
      REQUIRE(res4.has_value());
      CHECK(*res4 == 'd');
      CHECK(file.tell() == 5);
      CHECK(readable.position() == 4);
   }

   SUBCASE("End of stream") {
      for(int i = 0; i < 5; ++i) {
         REQUIRE(readable.read().has_value());
      }
      auto res = readable.read();
      REQUIRE(!res.has_value());
      CHECK(res.error() == Readable::EndOfStream);
   }

   SUBCASE("available() and bytesLeft()") {
      CHECK(readable.available());
      CHECK(readable.bytesLeft() == 5);

      readable.read();
      CHECK(readable.available());
      CHECK(readable.bytesLeft() == 4);

      for(int i = 0; i < 4; ++i)
         readable.read();
      CHECK(!readable.available());
      CHECK(readable.bytesLeft() == 0);
   }
}

TEST_CASE("[FatFileReadable] edge cases [FatFileReadable]") {
   FatFileMockGuard guard;
   auto &mockState = guard.state;
   mockState.fileData = { 'a', 'b', 'c' };

   FatFile file("test.txt", 0);

   SUBCASE("Empty buffer") {
      art::span<uint8_t> emptyBuffer;
      FatFileReadable readable(file, emptyBuffer);

      auto res = readable.read();
      REQUIRE(!res.has_value());
      CHECK(res.error() == Readable::ReadError);
   }
}

TEST_CASE("[FatFileReadable] seek and reset [FatFileReadable]") {
   FatFileMockGuard guard;
   auto &mockState = guard.state;
   mockState.fileData = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };

   FatFile file("test.txt", 0);
   uint8_t bufferMem[3];
   FatFileReadable readable(file, bufferMem);

   SUBCASE("Reset to beginning") {
      readable.read(); // '0'
      readable.read(); // '1'
      readable.reset(0);
      auto res = readable.read();
      REQUIRE(res.has_value());
      CHECK(*res == '0');
   }

   SUBCASE("Reset to middle") {
      readable.reset(5);
      auto res = readable.read();
      REQUIRE(res.has_value());
      CHECK(*res == '5');
      CHECK(readable.position() == 6);
   }

   SUBCASE("Reset within buffer optimization") {
      // Buffer size is 3.
      readable.read(); // '0', triggers read of '0', '1', '2' into buffer. file.tell() = 3.
      CHECK(file.tell() == 3);

      // Current position is 1 (after '0').
      // Reset to 2 (which is '2').
      // Since '2' is in the buffer (start=0, size=3), it shouldn't call file.seek().
      mockState.currentPosition = 3; // Ensure it matches

      readable.reset(2);
      CHECK(file.tell() == 3); // file.tell() should NOT have changed (no seek called if we are careful)
      // Actually, my implementation calls file.tell() - bytesBuffered.

      auto res = readable.read();
      REQUIRE(res.has_value());
      CHECK(*res == '2');
      CHECK(readable.position() == 3);
   }
}

TEST_CASE("[FatFileReadable] error handling [FatFileReadable]") {
   FatFileMockGuard guard;
   auto &mockState = guard.state;
   mockState.fileData = { 'a', 'b', 'c' };

   FatFile file("test.txt", 0);
   uint8_t bufferMem[3];
   FatFileReadable readable(file, bufferMem);

   SUBCASE("Read error") {
      mockState.nextReadResult = FR_DISK_ERR;
      auto res = readable.read();
      REQUIRE(!res.has_value());
      CHECK(res.error() == Readable::ReadError);
   }
}
