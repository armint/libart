#include "storage/FatFile.h"
#include "streams/FatFileWritable.h"
#include <catch2/catch_test_macros.hpp>
#include <vector>


TEST_CASE("FatFileWritable buffering and flush", "[FatFileWritable]") {
   FatFile::MockState mockState;
   FatFile::state = &mockState;

   FatFile file("test.txt", 0);
   uint8_t bufferMem[10];
   FatFileWritable writable(file, bufferMem);

   SECTION("Data is buffered and not written until buffer is full") {
      uint8_t data[] = { 1, 2, 3 };
      auto res = writable.write(data);
      REQUIRE(res.has_value());
      CHECK(mockState.writtenData.empty()); // Should be buffered
      CHECK(writable.position() == 3);
   }

   SECTION("Flush writes buffered data") {
      uint8_t data[] = { 1, 2, 3 };
      writable.write(data);
      auto res = writable.flush();
      REQUIRE(res.has_value());

      CHECK(mockState.writtenData.size() == 3);
      if(!mockState.writtenData.empty()) {
         CHECK(mockState.writtenData[0] == 1);
      }
      CHECK(mockState.syncCalled);
   }

   SECTION("Reset flushes buffer") {
      uint8_t data[] = { 1, 2, 3 };
      writable.write(data);
      writable.reset(0);

      CHECK(mockState.writtenData.size() == 3);
      CHECK(mockState.currentPosition == 0);
   }

   SECTION("Destructor flushes buffer") {
      {
         FatFileWritable writable2(file, bufferMem);
         uint8_t data[] = { 4, 5, 6 };
         writable2.write(data);
      }
      // Destructor should have called flush()
      CHECK(mockState.writtenData.size() == 3);
      CHECK(mockState.writtenData[0] == 4);
   }

   FatFile::state = nullptr;
}
