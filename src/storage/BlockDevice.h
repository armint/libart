#pragma once

#include "storage/StorageError.h"
#include <cstdint>
#include <expected>
#include <span>

namespace art::storage {

/**
 * @brief Byte offset into a block device.
 *
 * 64 bits wide so that SD cards above 4 GB remain addressable.
 */
using offset_t = uint64_t;

/**
 * @brief Physical layout and access granularity of a block device.
 *
 * The values returned by BlockDevice::geometry() are authoritative. Code above a device must not assume
 * the page or sector size of a particular driver.
 *
 * @par Invariant
 * `eraseBeforeProgram == true` requires `eraseSize > 0`, because there must be an erase unit to erase with.
 * Equivalently, `eraseSize == 0` implies `eraseBeforeProgram == false`. The defaults describe a RAM-like device
 * without erase that can be overwritten freely; a driver fills in the fields that differ.
 *
 * The isValid*() members are the single definition of the alignment and bounds rules of the BlockDevice
 * contract. Devices use them to reject bad calls with Error::InvalidArgument, and file systems use isValid()
 * to check the geometry they mount on. A zero-length range is valid.
 */
struct Geometry {
   /// Smallest readable unit in bytes. Read offsets and lengths are multiples of it.
   uint32_t readSize{ 1u };
   /// Smallest programmable unit in bytes (NOR page). Program offsets and lengths are multiples of it.
   uint32_t programSize{ 1u };
   /// Erase unit in bytes. 0 means the medium has no erase unit (RAM-like); erase() then has no effect.
   /// It must divide size (see isValid()), which is what NOR flash and littlefs need. A medium whose native
   /// erase or allocation unit does not divide its capacity, such as an SD card, reports 0 here: the device
   /// then exposes no erase (and no discard) through this interface.
   uint32_t eraseSize{ 0u };
   /// Total usable bytes.
   offset_t size{ 0u };
   /// true: a range must be erased before it is programmed (NOR). false: it can be overwritten (SD card, RAM).
   /// Requires eraseSize > 0. The two fields are independent otherwise: an SD card has an erase unit but does not
   /// require erase before program.
   bool eraseBeforeProgram{ false };

   /**
    * @brief Checks that the geometry is self-consistent.
    *
    * True if size, readSize and programSize are non-zero, the eraseBeforeProgram invariant holds, size is a
    * multiple of readSize and programSize, and, if there is an erase unit, eraseSize is a multiple of both and
    * divides size. A device with size 0 is never valid: it is what a failed capacity detection (unparsable SD
    * CSD, unreadable NOR JEDEC ID) looks like, and no useful device is empty. A file system should reject a
    * device whose geometry fails this check.
    */
   constexpr bool isValid() const {
      if(size == 0u || readSize == 0u || programSize == 0u || size % readSize != 0u || size % programSize != 0u) {
         return false;
      }
      if(eraseSize == 0u) {
         return !eraseBeforeProgram;
      }
      return eraseSize % readSize == 0u && eraseSize % programSize == 0u && size % eraseSize == 0u;
   }

   /// True if a read of `length` bytes at `offset` is aligned to readSize and lies within size.
   constexpr bool isValidRead(offset_t offset, offset_t length) const {
      return isValidRange(offset, length, readSize);
   }

   /// True if a program of `length` bytes at `offset` is aligned to programSize and lies within size.
   constexpr bool isValidProgram(offset_t offset, offset_t length) const {
      return isValidRange(offset, length, programSize);
   }

   /// True if an erase of `length` bytes at `offset` is aligned to eraseSize and lies within size. Without an
   /// erase unit (eraseSize 0) any range within size is valid.
   constexpr bool isValidErase(offset_t offset, offset_t length) const {
      return isValidRange(offset, length, eraseSize == 0u ? 1u : eraseSize);
   }

   private:
   // Written as `length <= size - offset` so that offset + length cannot overflow 64 bits.
   constexpr bool isValidRange(offset_t offset, offset_t length, uint32_t granularity) const {
      return granularity != 0u && offset % granularity == 0u && length % granularity == 0u && offset <= size &&
             length <= size - offset;
   }
};

/**
 * @brief Abstract, byte-addressed storage device that a file system (or another device) sits on.
 *
 * A file system talks to a BlockDevice instead of a concrete driver. Concrete drivers (SPI NOR, SD card,
 * RAM) implement this interface. Transform devices (encryption, whitening) implement it too and are stacked
 * on top of another device with BlockDeviceDecorator, so neither the file system nor the chip driver
 * knows about them.
 *
 * All positions are byte offsets, not sector numbers. NOR flash is byte addressable and file systems such as
 * LittleFS read metadata in units far smaller than a sector; sector addressing would inflate each of those
 * reads to a full sector. A sector-based consumer such as FAT converts with a single multiplication.
 *
 * @par Thread-safety and serialization contract
 * A BlockDevice is @b not required to be thread-safe. The layer above must serialize all calls to one
 * instance (init, read, program, erase, sync and geometry), including calls arriving from different tasks.
 * Implementations may therefore keep unsynchronized per-instance state. In particular, a transform
 * decorator may own a single scratch buffer: it cannot encrypt in place because program() takes a const
 * span, so it needs one.
 *
 * @par Erase and program semantics
 * On NOR flash program() can only clear bits, so the target range must have been erased before.
 * Geometry::eraseBeforeProgram tells the layer above which semantics apply, which lets one RAM device emulate both
 * NOR and an SD card. The method is called program() and not write() on purpose, to make this
 * distinction visible at every call site.
 *
 * @par Alignment
 * Callers pass offsets and lengths that are multiples of the matching Geometry granularity (readSize,
 * programSize, eraseSize) and lie within Geometry::size. The requirement is unconditional: no device
 * relaxes it, so generic code above the seam can rely on it. Every device rejects violations with
 * Error::InvalidArgument, using Geometry::isValidRead(), isValidProgram() and isValidErase().
 */
class BlockDevice {
   public:
   virtual ~BlockDevice() = default;

   protected:
   // Polymorphic interface: no public copy or move, so that copying a derived device through a base reference
   // (or a decorator by value) cannot silently slice it. Derived classes decide whether they are copyable.
   BlockDevice() = default;
   BlockDevice(const BlockDevice &) = default;
   BlockDevice(BlockDevice &&) = default;
   BlockDevice &operator=(const BlockDevice &) = default;
   BlockDevice &operator=(BlockDevice &&) = default;

   public:
   /**
    * @brief Prepares the device for use (bus setup, chip identification, ...).
    *
    * Must be called, and must succeed, before any other method, geometry() included: a device may learn its
    * capacity only during init() (SD card CSD, NOR JEDEC ID), so geometry() has no defined value before
    * that. Calling init() again on an initialised device is allowed and must leave the stored data untouched.
    *
    * @return Nothing on success, otherwise the reason for the failure (for example Error::Io).
    */
   virtual std::expected<void, Error> init() = 0;

   /**
    * @brief Reads bytes from the device.
    *
    * @param offset Byte offset of the first byte to read.
    * @param buffer Destination. Its size is the number of bytes read.
    * @return Nothing on success, otherwise the error. The buffer content is unspecified after an error.
    */
   virtual std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) = 0;

   /**
    * @brief Programs bytes into the device.
    *
    * If Geometry::eraseBeforeProgram is true, the target range must have been erased and not programmed
    * since. Otherwise the outcome is unspecified.
    *
    * @param offset Byte offset of the first byte to program.
    * @param data Source. Its size is the number of bytes programmed.
    * @return Nothing on success, otherwise the error. The target range is in an undefined state after an error.
    */
   virtual std::expected<void, Error> program(offset_t offset, std::span<const uint8_t> data) = 0;

   /**
    * @brief Erases a range of the device.
    *
    * After a successful erase on a device with Geometry::eraseSize > 0 the range reads back in the erased
    * state of the medium (typically 0xFF on NOR). On a device with Geometry::eraseSize of 0 there is no erase
    * unit: an in-bounds call succeeds without changing the stored data, and the caller must not rely on it to
    * change what read() returns.
    *
    * @param offset Byte offset of the range start, a multiple of Geometry::eraseSize (any offset if it is 0).
    * @param length Length of the range in bytes, a multiple of Geometry::eraseSize (any length if it is 0).
    * @return Nothing on success, otherwise the error.
    */
   virtual std::expected<void, Error> erase(offset_t offset, offset_t length) = 0;

   /**
    * @brief Flushes buffered writes to the medium.
    *
    * The default implementation does nothing and succeeds, which is correct for devices without write caching.
    * Devices or decorators that buffer data override it and must forward the call to the device below.
    *
    * @return Nothing on success, otherwise the error.
    */
   virtual std::expected<void, Error> sync() {
      return {};
   }

   /**
    * @brief Describes the layout and access granularity of the device.
    *
    * The result is authoritative. A transform device may rewrite it, for example a cipher raises readSize
    * and programSize to its sector size and passes eraseSize and size through.
    *
    * Only valid after a successful init(); calling it earlier is a contract violation and the result is
    * unspecified. Consumers therefore never have to tell "unknown" from "empty".
    *
    * @return The device geometry. It must not change after a successful init().
    */
   virtual Geometry geometry() const = 0;
};

} // namespace art::storage
