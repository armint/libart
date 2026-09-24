#pragma once

#include "storage/BlockDevice.h"
#include "storage/RamBlockDevice.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <span>

namespace art::storage::test {

/**
 * @brief 1 MiB RAM disk configured like an SD card, for the FatFs test suites.
 *
 * 512 byte sectors, overwritable (Geometry::eraseBeforeProgram false) and a 4 KiB erase unit, so that FatFs
 * sees a device with a real erase block size (GET_BLOCK_SIZE). Every instance starts erased. The backing
 * array is shared, so only one FatTestDisk may be alive at a time: the constructor fails the test if another
 * instance is alive. The fixtures of the FatFs tests keep to that, since FatFs registers drive 0 for the lifetime
 * of a fixture.
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

   // Checked before the base class fills the shared backing array, which would wipe a live instance.
   FatTestDisk()
      : RamBlockDevice(kGeometry, claimBacking()) {}

   FatTestDisk(const FatTestDisk &) = delete;
   FatTestDisk &operator=(const FatTestDisk &) = delete;

   ~FatTestDisk() override {
      --instances_;
   }

   private:
   static std::span<uint8_t> claimBacking() {
      REQUIRE_MESSAGE(instances_ == 0, "only one FatTestDisk may be alive: the backing array is shared");
      ++instances_;
      return backing_;
   }

   static inline std::array<uint8_t, kSize> backing_{};
   static inline uint32_t instances_{ 0u };
};

} // namespace art::storage::test
