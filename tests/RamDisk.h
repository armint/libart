#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

/**
 * In-memory block device backing store for unit tests.
 * Provides a flat sector array accessed by the diskio_ram implementation.
 */
class RamDisk {
public:
   static constexpr size_t SECTOR_SIZE = 512;
   static constexpr size_t SECTOR_COUNT = 2048; // 1 MB

   std::array<uint8_t, SECTOR_SIZE * SECTOR_COUNT> sectors{};
   bool initialized = false;

   void reset() {
      sectors.fill(0);
      initialized = false;
   }

   static RamDisk &instance() {
      static RamDisk disk;
      return disk;
   }

private:
   RamDisk() = default;
};
