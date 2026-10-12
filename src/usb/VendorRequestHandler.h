#pragma once

#include "usb/UsbTypes.h"
#include "utils/compat/span.h"
#include <cstdint>
#include <optional>

namespace art::usb {

/**
 * @brief Handles the vendor requests whose recipient is the device (bmRequestType 0x40 and 0xC0).
 *
 * A device-recipient vendor request belongs to no function, so usb::Device passes it to the handler that
 * Device::setVendorRequestHandler() registers. Without a handler such a request stalls. An example is the request
 * of Windows for the MS OS 2.0 descriptor set (usb/MsOs20.h).
 *
 * usb::Device calls the handler in the states Address and Configured: Windows reads the MS OS 2.0 descriptor set
 * before SET_CONFIGURATION. In the other states the request stalls without a call. The call runs in the context that
 * delivers the controller events (normally the main loop).
 */
class VendorRequestHandler {
   public:
   virtual ~VendorRequestHandler() = default;

   VendorRequestHandler() = default;
   VendorRequestHandler(const VendorRequestHandler &) = delete;
   VendorRequestHandler &operator=(const VendorRequestHandler &) = delete;

   /**
    * @brief Handles one device-recipient vendor request.
    *
    * Host-to-device requests are passed after their data stage, device-to-host requests at once.
    *
    * @param setup The request.
    * @param buffer Host-to-device: the received data stage (exactly setup.wLength bytes, empty without data stage).
    * Device-to-host: a buffer of min(setup.wLength, Device::kControlBufferSize) bytes for a reply that the handler
    * builds at run time.
    * @return Device-to-host: the reply. It is a part of `buffer`, or memory that stays valid until the next SETUP
    * packet (for example a descriptor in flash), so a reply can be longer than the buffer. usb::Device sends at most
    * setup.wLength bytes of it. Host-to-device: any value (normally an empty span) accepts the request.
    * std::nullopt stalls the request in both directions.
    */
   virtual std::optional<art::span<const uint8_t>> vendorRequest(const SetupPacket &setup, art::span<uint8_t> buffer) = 0;
};

} // namespace art::usb
