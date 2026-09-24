#pragma once

#include "storage/BlockDevice.h"
#include "storage/RamBlockDevice.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace art::storage::test {

/**
 * @brief 1 MiB RAM disk configured like an SD card, for the FatFs test suites.
 *
 * 512 byte sectors, overwritable (Geometry::eraseBeforeProgram false) and a 4 KiB erase unit, so that FatFs
 * sees a device with a real erase block size (GET_BLOCK_SIZE). Every instance starts erased. The backing
 * array is shared, so only one FatTestDisk may be alive at a time, which the fixtures of the FatFs tests
 * guarantee: FatFs registers drive 0 for the lifetime of a fixture.
 */
class FatTestDisk : public RamBlockDevice {
   public:
   static constexpr uint32_t kSectorSize = 512u;
   static constexpr offset_t kSize = 1024u * 1024u;

   static constexpr Geometry kGeometry{
      .readSize = kSectorSize,
      .programSize = kSectorSize,
      .eraseSize = 4096u,
      .size = kSize,
      .eraseBeforeProgram = false,
   };

   FatTestDisk()
      : RamBlockDevice(kGeometry, backing_) {}

   private:
   static inline std::array<uint8_t, kSize> backing_{};
};

} // namespace art::storage::test
