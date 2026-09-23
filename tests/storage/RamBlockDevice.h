#pragma once

#include "storage/BlockDevice.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace art::storage::test {

/**
 * @brief BlockDevice test double that models real NOR flash semantics on the build machine.
 *
 * It is deliberately not a forgiving byte array:
 * - erase() fills the range with 0xFF.
 * - program() ANDs the data into the existing bytes when Geometry::eraseBeforeProgram is true, so programming
 *   over un-erased cells yields the bitwise AND, exactly as the chip does. With eraseBeforeProgram false
 *   (SD-card-like) program() overwrites.
 * - Alignment to readSize, programSize and eraseSize is enforced and violations are rejected with
 *   Error::InvalidArgument, as is any range that leaves the device (Geometry::isValidRead(), isValidProgram()
 *   and isValidErase() define the rules). On a device without erase unit only the range is checked.
 * - Methods other than geometry() fail with Error::NotInitialised until init() succeeded.
 *
 * The device itself never allocates. The caller supplies the backing storage, which must stay valid for the
 * lifetime of the device and hold at least Geometry::size bytes.
 *
 * @par Fault injection
 * failNthOperation() makes one operation fail with Error::Io without touching the data. It counts the same
 * operations as the counters below, so erase() on a device without erase unit never consumes the countdown.
 * truncateNextProgram() simulates a power cut in the middle of a program(): only the first bytes are written
 * before the call fails with Error::Io.
 *
 * @par Counters
 * readCount(), programCount() and eraseCount() count operations that reached the medium, that is, calls on an
 * initialised device with valid arguments (including ones that then failed through fault injection). Rejected
 * calls and erase() on a device without erase unit (Geometry::eraseSize 0) are not counted.
 */
class RamBlockDevice : public BlockDevice {
   public:
   /// Content of a cell after erase() and of a freshly constructed device.
   static constexpr uint8_t kErasedByte = 0xFFu;

   /**
    * @brief Creates a device and fills the backing storage with the erased pattern.
    *
    * @param geometry Geometry reported by geometry(). init() rejects one that fails Geometry::isValid().
    * @param backing Storage for the device content. Referenced, not copied; init() rejects storage smaller
    * than geometry.size.
    */
   RamBlockDevice(const Geometry &geometry, std::span<uint8_t> backing)
      : geometry_(geometry)
      , backing_(backing) {
      std::ranges::fill(backing_, kErasedByte);
   }

   std::expected<void, Error> init() override {
      if(!geometry_.isValid() || backing_.size() < geometry_.size) {
         return std::unexpected(Error::InvalidArgument);
      }
      initialised_ = true;
      return {};
   }

   std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) override {
      if(auto admitted = admit(geometry_.isValidRead(offset, buffer.size()), readCount_); !admitted) {
         return admitted;
      }
      std::memcpy(buffer.data(), backing_.data() + offset, buffer.size());
      return {};
   }

   std::expected<void, Error> program(offset_t offset, std::span<const uint8_t> data) override {
      if(auto admitted = admit(geometry_.isValidProgram(offset, data.size()), programCount_); !admitted) {
         return admitted;
      }
      size_t bytesToWrite = data.size();
      bool powerCut = false;
      if(truncateProgramAfter_) {
         bytesToWrite = std::min(bytesToWrite, *truncateProgramAfter_);
         truncateProgramAfter_.reset();
         powerCut = true;
      }
      for(size_t index = 0u; index < bytesToWrite; ++index) {
         uint8_t &cell = backing_[offset + index];
         cell = geometry_.eraseBeforeProgram ? static_cast<uint8_t>(cell & data[index]) : data[index];
      }
      if(powerCut) {
         return std::unexpected(Error::Io);
      }
      return {};
   }

   std::expected<void, Error> erase(offset_t offset, offset_t length) override {
      if(geometry_.eraseSize == 0u) {
         // No erase unit: an in-bounds call succeeds without touching the data and is not counted.
         if(!initialised_) {
            return std::unexpected(Error::NotInitialised);
         }
         if(!geometry_.isValidErase(offset, length)) {
            return std::unexpected(Error::InvalidArgument);
         }
         return {};
      }
      if(auto admitted = admit(geometry_.isValidErase(offset, length), eraseCount_); !admitted) {
         return admitted;
      }
      std::fill_n(backing_.begin() + offset, length, kErasedByte);
      return {};
   }

   Geometry geometry() const override {
      return geometry_;
   }

   /**
    * @brief Makes the n-th following read, program or erase fail with Error::Io, leaving the data untouched.
    *
    * Only operations that are counted (see the class description) take part: erase() on a device without erase
    * unit does not. 1 is the next operation. The failure is one-shot: later operations succeed again. 0 disarms.
    */
   void failNthOperation(uint32_t n) {
      operationsUntilFailure_ = n;
   }

   /**
    * @brief Makes the next program() write only its first bytes and then fail with Error::Io.
    *
    * Simulates a power cut mid-write: exactly `bytesWritten` bytes (fewer if the call is shorter) are ANDed
    * into the medium. One-shot. A failure armed with failNthOperation() takes precedence for that call and
    * leaves this one armed.
    */
   void truncateNextProgram(size_t bytesWritten) {
      truncateProgramAfter_ = bytesWritten;
   }

   /// Disarms failNthOperation() and truncateNextProgram().
   void clearFaults() {
      operationsUntilFailure_ = 0u;
      truncateProgramAfter_.reset();
   }

   uint32_t readCount() const {
      return readCount_;
   }

   uint32_t programCount() const {
      return programCount_;
   }

   uint32_t eraseCount() const {
      return eraseCount_;
   }

   void resetCounters() {
      readCount_ = 0u;
      programCount_ = 0u;
      eraseCount_ = 0u;
   }

   /// Raw content of the backing storage, for tests that inspect what really reached the medium.
   std::span<const uint8_t> contents() const {
      return backing_;
   }

   std::span<uint8_t> contents() {
      return backing_;
   }

   private:
   // Common front end of read, program and erase: initialisation, argument validation (the caller passes the
   // Geometry::isValid*() verdict), counting and the n-th-operation fault.
   std::expected<void, Error> admit(bool validArguments, uint32_t &counter) {
      if(!initialised_) {
         return std::unexpected(Error::NotInitialised);
      }
      if(!validArguments) {
         return std::unexpected(Error::InvalidArgument);
      }
      ++counter;
      if(operationsUntilFailure_ != 0u && --operationsUntilFailure_ == 0u) {
         return std::unexpected(Error::Io);
      }
      return {};
   }

   Geometry geometry_;
   std::span<uint8_t> backing_;
   bool initialised_{ false };
   uint32_t readCount_{ 0u };
   uint32_t programCount_{ 0u };
   uint32_t eraseCount_{ 0u };
   uint32_t operationsUntilFailure_{ 0u };
   std::optional<size_t> truncateProgramAfter_;
};

} // namespace art::storage::test
