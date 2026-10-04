#pragma once

#include "usb/UsbTypes.h"
#include "utils/compat/span.h"
#include <cstdint>
#include <optional>

namespace art::usb {

class Device;

/**
 * @brief One function of a USB device (for example CDC-ACM or mass storage) that owns a contiguous range of
 * interfaces.
 *
 * A function is registered with Device::addFunction(). usb::Device then forwards to it:
 * - class, vendor and unknown standard requests whose recipient is one of its interfaces, or an endpoint of one of
 *   its interfaces;
 * - the packets and completions of the endpoints of its interfaces;
 * - configuration, alternate-setting, reset and suspend changes.
 *
 * The endpoints of the active alternate settings are opened and closed by usb::Device, from the configuration
 * descriptor. A function sends and receives with Device::startInTransfer() and Device::armOutTransfer(), and halts
 * an endpoint with Device::haltEndpoint().
 *
 * All callbacks run in the context that delivers the controller events (normally the main loop). The default
 * implementations do nothing, and controlRequest() stalls.
 */
class Function {
   public:
   virtual ~Function() = default;

   /**
    * @param firstInterface The bInterfaceNumber of the first interface of the function.
    * @param interfaceCount The number of interfaces, at least 1.
    */
   Function(uint8_t firstInterface, uint8_t interfaceCount)
      : firstInterface_(firstInterface)
      , interfaceCount_(interfaceCount) {
   }

   Function(const Function &) = delete;
   Function &operator=(const Function &) = delete;

   [[nodiscard]] uint8_t firstInterface() const {
      return firstInterface_;
   }

   [[nodiscard]] uint8_t interfaceCount() const {
      return interfaceCount_;
   }

   /// True if the function owns the interface number.
   [[nodiscard]] bool ownsInterface(uint8_t interfaceNumber) const {
      return interfaceNumber >= firstInterface_ && interfaceNumber - firstInterface_ < interfaceCount_;
   }

   /**
    * @brief Handles a control request addressed to the function.
    *
    * Host-to-device requests are passed after their data stage. Device-to-host requests are passed at once, and
    * the function writes its reply into `data`.
    *
    * @param setup The request.
    * @param data Host-to-device: the received data stage (exactly setup.wLength bytes; empty without data stage). A shorter data stage stalls without a call.
    * Device-to-host: a buffer of min(setup.wLength, Device::kControlBufferSize) bytes for the reply.
    * @return The number of reply bytes in `data` (device-to-host; at most data.size()) or 0 (host-to-device) to
    * accept the request. std::nullopt stalls it.
    */
   virtual std::optional<uint16_t> controlRequest(const SetupPacket &setup, art::span<uint8_t> data) {
      static_cast<void>(setup);
      static_cast<void>(data);
      return std::nullopt;
   }

   /// SET_CONFIGURATION with a non-zero value: the endpoints of alternate setting 0 are open.
   virtual void configured() {
   }

   /// Bus reset, or SET_CONFIGURATION that leaves the configured state: the endpoints of the function are closed.
   virtual void deconfigured() {
   }

   /// SET_INTERFACE: the endpoints of the new alternate setting of the interface are open.
   virtual void alternateSettingChanged(uint8_t interfaceNumber, uint8_t alternateSetting) {
      static_cast<void>(interfaceNumber);
      static_cast<void>(alternateSetting);
   }

   /// The bus is suspended. Without VBUS sense this is also what an unplugged cable looks like.
   virtual void suspended() {
   }

   /// The bus is active again after suspended().
   virtual void resumed() {
   }

   /// An OUT packet arrived on an endpoint of the function (see ControllerEvents::outPacketReceived()).
   virtual void outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) {
      static_cast<void>(endpointAddress);
      static_cast<void>(packet);
   }

   /// The host acknowledged the last IN packet of an endpoint of the function.
   virtual void inPacketSent(uint8_t endpointAddress) {
      static_cast<void>(endpointAddress);
   }

   /**
    * @brief The host cleared the halt of an endpoint of the function (CLEAR_FEATURE(ENDPOINT_HALT)).
    *
    * The endpoint is no longer stalled and its data toggle is DATA0. A function that must keep the endpoint halted
    * (for example mass storage before a reset recovery) calls Device::haltEndpoint() again here; this runs before the
    * status stage, so the host sees no window without the stall.
    */
   virtual void endpointHaltCleared(uint8_t endpointAddress) {
      static_cast<void>(endpointAddress);
   }

   protected:
   /// The device the function is registered with, nullptr before Device::addFunction().
   [[nodiscard]] Device *device() const {
      return device_;
   }

   private:
   friend class Device;

   uint8_t firstInterface_;
   uint8_t interfaceCount_;
   Device *device_{ nullptr };
};

} // namespace art::usb
