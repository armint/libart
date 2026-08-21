#pragma once
#include "storage/StorageDevice.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

/**
 * In-memory block device backing store for unit tests. Implements
 * StorageDevice so it can be registered with FatFileSystem the same way a
 * real SD card / flash device would be, letting fatfs's disk_*() calls
 * (routed through FatFileSystem::devices[]) reach a flat sector array
 * instead of real hardware.
 */
class RamDisk : public StorageDevice {
public:
   static constexpr size_t SECTOR_SIZE = 512;
   static constexpr size_t SECTOR_COUNT = 2048; // 1 MB

   // Matches fatfs/diskio.h's STA_NOINIT (0x01). Not included directly:
   // diskio.h requires ff.h's integer typedefs (BYTE, ...) to already be
   // visible, making its own inclusion order fragile from a standalone
   // header like this one.
   static constexpr uint32_t kNotInitializedStatus = 0x01;

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

   uint32_t initialize() override {
      initialized = true;
      return 0;
   }

   uint32_t getStatus() override {
      return initialized ? 0 : kNotInitializedStatus;
   }

   bd_size_t read(uint8_t *buffer, bd_size_t sector, bd_size_t count) override {
      if(static_cast<size_t>(sector) + count > SECTOR_COUNT) {
         return 0;
      }
      std::memcpy(buffer, sectors.data() + static_cast<size_t>(sector) * SECTOR_SIZE, static_cast<size_t>(count) * SECTOR_SIZE);
      return count;
   }

   bd_size_t write(const uint8_t *buffer, bd_size_t sector, bd_size_t count) override {
      if(static_cast<size_t>(sector) + count > SECTOR_COUNT) {
         return 0;
      }
      std::memcpy(sectors.data() + static_cast<size_t>(sector) * SECTOR_SIZE, buffer, static_cast<size_t>(count) * SECTOR_SIZE);
      return count;
   }

   bd_size_t getSectorCount() override {
      return SECTOR_COUNT;
   }

   bd_size_t getEraseSize() override {
      // No real erase-block granularity for a RAM-backed device, unlike
      // flash/SD; DEFAULT_ERASE_SIZE (4096, i.e. 8-sector alignment) makes
      // f_mkfs's auto cluster sizing fail (FR_MKFS_ABORTED) on this
      // volume's modest 1 MB size.
      return SECTOR_SIZE;
   }

   bd_size_t erase(bd_size_t start, bd_size_t count) override {
      if(static_cast<size_t>(start) + count > SECTOR_COUNT) {
         return 0;
      }
      std::memset(sectors.data() + static_cast<size_t>(start) * SECTOR_SIZE, 0, static_cast<size_t>(count) * SECTOR_SIZE);
      return count;
   }

   bd_size_t getBlockSize() const override {
      return SECTOR_SIZE;
   }

private:
   RamDisk() = default;
};
