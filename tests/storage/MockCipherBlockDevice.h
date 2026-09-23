#pragma once

#include "storage/BlockDeviceDecorator.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace art::storage::test {

/**
 * @brief Deliberately trivial transform device: XORs every byte with a keystream that depends only on its
 * absolute offset.
 *
 * It exists to prove that a transform device stacks on another BlockDevice and passes the same contract suite
 * as a real driver, without pulling a crypto implementation into the test build. It has the shape of a real
 * sector cipher: it raises readSize and programSize to kSectorSize (and rejects anything not aligned to it
 * itself, even if the device below would tolerate it), passes eraseSize and size through, and needs a scratch
 * buffer because program() takes a const span.
 *
 * The device below must have readSize and programSize dividing kSectorSize and an eraseSize that is a
 * multiple of it; init() returns Error::Unsupported otherwise.
 *
 * @note Erase is forwarded unchanged, so an erased range reads back as the decrypted erased pattern rather
 * than 0xFF, as with any real cipher on top of NOR flash.
 */
class MockCipherBlockDevice : public BlockDeviceDecorator {
   public:
   /// Alignment the cipher imposes on offsets and lengths of read() and program().
   static constexpr uint32_t kSectorSize = 16u;

   using BlockDeviceDecorator::BlockDeviceDecorator;

   std::expected<void, Error> init() override {
      if(auto initialised = lower().init(); !initialised) {
         return initialised;
      }
      const Geometry below = lower().geometry();
      if(!below.isValid() || kSectorSize % below.readSize != 0u || kSectorSize % below.programSize != 0u ||
         below.eraseSize % kSectorSize != 0u) {
         return std::unexpected(Error::Unsupported);
      }
      return {};
   }

   std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) override {
      if(!geometry().isValidRead(offset, buffer.size())) {
         return std::unexpected(Error::InvalidArgument);
      }
      if(auto result = lower().read(offset, buffer); !result) {
         return result;
      }
      applyKeystream(offset, buffer);
      return {};
   }

   std::expected<void, Error> program(offset_t offset, std::span<const uint8_t> data) override {
      // Validate up front so a rejected call writes nothing, even though the data is encrypted in chunks.
      if(!geometry().isValidProgram(offset, data.size())) {
         return std::unexpected(Error::InvalidArgument);
      }
      size_t done = 0u;
      while(done < data.size()) {
         const size_t chunk = std::min(scratch_.size(), data.size() - done);
         std::copy_n(data.begin() + done, chunk, scratch_.begin());
         applyKeystream(offset + done, std::span<uint8_t>{ scratch_.data(), chunk });
         if(auto result = lower().program(offset + done, std::span<const uint8_t>{ scratch_.data(), chunk }); !result) {
            return result;
         }
         done += chunk;
      }
      return {};
   }

   Geometry geometry() const override {
      Geometry inner = lower().geometry();
      inner.readSize = std::max(inner.readSize, kSectorSize);
      inner.programSize = std::max(inner.programSize, kSectorSize);
      return inner;
   }

   private:
   static constexpr uint8_t keyByte(offset_t position) {
      return static_cast<uint8_t>((position * 167u + 0x5Au) ^ (position >> 8u));
   }

   static void applyKeystream(offset_t offset, std::span<uint8_t> bytes) {
      for(size_t index = 0u; index < bytes.size(); ++index) {
         bytes[index] ^= keyByte(offset + index);
      }
   }

   // Multiple of kSectorSize, so every chunk stays sector aligned.
   std::array<uint8_t, 4u * kSectorSize> scratch_{};
};

} // namespace art::storage::test
