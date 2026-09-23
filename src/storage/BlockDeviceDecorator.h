#pragma once

#include "storage/BlockDevice.h"

namespace art::storage {

/**
 * @brief Base class for devices that wrap another BlockDevice.
 *
 * Every method forwards to the lower device unchanged. A transform device (encryption, whitening, address
 * remapping, ...) derives from this class and overrides only what it changes, usually read(), program() and
 * geometry(). It can then be placed between a file system and a chip driver without either side knowing.
 *
 * @code
 * class XorDevice : public art::storage::BlockDeviceDecorator {
 *    public:
 *    using BlockDeviceDecorator::BlockDeviceDecorator;
 *
 *    std::expected<void, art::storage::Error> read(art::storage::offset_t offset, std::span<uint8_t> buffer) override;
 *    std::expected<void, art::storage::Error> program(art::storage::offset_t offset, std::span<const uint8_t> data)
 * override; art::storage::Geometry geometry() const override;
 * };
 * @endcode
 *
 * @note Same serialization contract as BlockDevice: calls on the decorator and on the lower device are
 * serialized by the layer above, so a decorator may keep an unsynchronized scratch buffer.
 *
 * @note The decorator does not own the lower device. The lower device must outlive the decorator and must
 * not be used directly while the decorator is in use.
 */
class BlockDeviceDecorator : public BlockDevice {
   public:
   /**
    * @brief Creates a decorator on top of a lower device.
    *
    * @param lower The device to forward to. Stored by reference; it must outlive this object.
    */
   explicit BlockDeviceDecorator(BlockDevice &lower)
      : lower_(lower) {}

   /// Forwards to the lower device.
   std::expected<void, Error> init() override {
      return lower_.init();
   }

   /// Forwards to the lower device.
   std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) override {
      return lower_.read(offset, buffer);
   }

   /// Forwards to the lower device.
   std::expected<void, Error> program(offset_t offset, std::span<const uint8_t> data) override {
      return lower_.program(offset, data);
   }

   /// Forwards to the lower device.
   std::expected<void, Error> erase(offset_t offset, offset_t length) override {
      return lower_.erase(offset, length);
   }

   /// Forwards to the lower device.
   std::expected<void, Error> sync() override {
      return lower_.sync();
   }

   /// Forwards to the lower device.
   Geometry geometry() const override {
      return lower_.geometry();
   }

   protected:
   /// The device below. Available to derived classes for their own forwarding logic.
   BlockDevice &lower() {
      return lower_;
   }

   /// The device below (const access).
   const BlockDevice &lower() const {
      return lower_;
   }

   private:
   BlockDevice &lower_;
};

} // namespace art::storage
