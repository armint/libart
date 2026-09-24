#pragma once

#include "storage/BlockDeviceDecorator.h"
#include <cstdint>
#include <optional>

namespace art::storage::test {

// Lets failAfter reads through, then fails every read with Timeout until disarm().
class FlakyReadDevice final : public BlockDeviceDecorator {
   public:
   using BlockDeviceDecorator::BlockDeviceDecorator;

   std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) override {
      if(failAfter) {
         if(*failAfter == 0u) {
            return std::unexpected(Error::Timeout);
         }
         --*failAfter;
      }
      return BlockDeviceDecorator::read(offset, buffer);
   }

   void armAfter(uint32_t reads) {
      failAfter = reads;
   }
   void disarm() {
      failAfter.reset();
   }

   private:
   std::optional<uint32_t> failAfter;
};

} // namespace art::storage::test
