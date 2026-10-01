#pragma once

#include "RamBlockDevice.h"
#include "storage/BlockDeviceDecorator.h"
#include "utils/compat/span.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace art::storage::test {

/**
 * Simulates a power cut on the medium. It counts program and erase operations. When the armed operation is
 * reached, that operation is torn (only its first tearBytes bytes take effect) and fails with Error::Io, and
 * from then on every program, erase and sync fails, as if the power were gone. Reads still work. restorePower()
 * ends the outage.
 */
class PowerCutDevice final : public BlockDeviceDecorator {
   public:
   explicit PowerCutDevice(RamBlockDevice &ram)
      : BlockDeviceDecorator(ram)
      , ram_(ram) {}

   /// The `writeOperation`-th following program or erase (1 is the next one) is torn after `tearBytes`.
   void armCut(uint32_t writeOperation, size_t tearBytes) {
      writeOperationsUntilCut_ = writeOperation;
      tearBytes_ = tearBytes;
   }

   void restorePower() {
      writeOperationsUntilCut_ = 0u;
      cut_ = false;
   }

   [[nodiscard]] bool hasCut() const {
      return cut_;
   }

   /// Program and erase operations since the last resetCount().
   [[nodiscard]] uint32_t writeOperations() const {
      return writeOperations_;
   }

   void resetCount() {
      writeOperations_ = 0u;
   }

   art::expected<void, Error> program(offset_t offset, art::span<const uint8_t> data) override {
      if(cut_) {
         return art::unexpected(Error::Io);
      }
      ++writeOperations_;
      if(writeOperationsUntilCut_ != 0u && --writeOperationsUntilCut_ == 0u) {
         cut_ = true;
         ram_.truncateNextProgram(tearBytes_);
         static_cast<void>(BlockDeviceDecorator::program(offset, data));
         ram_.clearFaults();
         return art::unexpected(Error::Io);
      }
      return BlockDeviceDecorator::program(offset, data);
   }

   art::expected<void, Error> erase(offset_t offset, offset_t length) override {
      if(cut_) {
         return art::unexpected(Error::Io);
      }
      ++writeOperations_;
      if(writeOperationsUntilCut_ != 0u && --writeOperationsUntilCut_ == 0u) {
         cut_ = true;
         const size_t erased = std::min<size_t>(tearBytes_, length);
         std::ranges::fill(ram_.contents().subspan(static_cast<size_t>(offset), erased), RamBlockDevice::kErasedByte);
         return art::unexpected(Error::Io);
      }
      return BlockDeviceDecorator::erase(offset, length);
   }

   art::expected<void, Error> sync() override {
      if(cut_) {
         return art::unexpected(Error::Io);
      }
      return BlockDeviceDecorator::sync();
   }

   private:
   RamBlockDevice &ram_;
   bool cut_{ false };
   uint32_t writeOperationsUntilCut_{ 0u };
   size_t tearBytes_{ 0u };
   uint32_t writeOperations_{ 0u };
};

} // namespace art::storage::test
