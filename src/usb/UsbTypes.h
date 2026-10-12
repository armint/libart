#pragma once

#include "utils/compat/span.h"
#include <cstddef>
#include <cstdint>

/**
 * @file
 * @brief Constants and small value types of the USB 2.0 specification, chapter 9, used by the platform-independent
 * USB device stack (usb::Device, usb::DeviceController, usb::Function).
 */

namespace art::usb {

/// Endpoint address bit that selects the IN (device-to-host) direction.
constexpr uint8_t kEndpointDirectionIn = 0x80u;
/// Mask of the endpoint number in an endpoint address.
constexpr uint8_t kEndpointNumberMask = 0x0Fu;
/// Number of endpoint numbers of a USB device (0 to 15).
constexpr uint8_t kMaxEndpointNumbers = 16u;

/// Returns true if the endpoint address is an IN (device-to-host) endpoint.
constexpr bool isInEndpoint(uint8_t endpointAddress) {
   return (endpointAddress & kEndpointDirectionIn) != 0u;
}

/// Returns the endpoint number (0 to 15) of an endpoint address.
constexpr uint8_t endpointNumber(uint8_t endpointAddress) {
   return endpointAddress & kEndpointNumberMask;
}

/// Transfer type of an endpoint, as in bits 1..0 of bmAttributes of the endpoint descriptor.
enum class EndpointType : uint8_t {
   Control = 0u,
   Isochronous = 1u,
   Bulk = 2u,
   Interrupt = 3u,
};

/// What DeviceController::openEndpoint() needs to set up one endpoint.
struct EndpointConfig {
   /// Endpoint address: number in bits 3..0, direction in bit 7. A control endpoint is opened once, with its OUT
   /// address (for example 0x00 for EP0), and covers both directions.
   uint8_t address{ 0u };
   EndpointType type{ EndpointType::Control };
   /// Maximum packet size in bytes (wMaxPacketSize).
   uint16_t maxPacketSize{ 0u };
};

/// Descriptor types (wValue high byte of GET_DESCRIPTOR, bDescriptorType of a descriptor).
enum class DescriptorType : uint8_t {
   Device = 1u,
   Configuration = 2u,
   String = 3u,
   Interface = 4u,
   Endpoint = 5u,
   DeviceQualifier = 6u,
   OtherSpeedConfiguration = 7u,
   InterfacePower = 8u,
   InterfaceAssociation = 11u,
   /// Binary device object store (USB 3.2 section 9.6.2, USB 2.0 LPM ECN). A USB 2.0 device has one if bcdUSB is
   /// 0x0201 or more.
   Bos = 15u,
   /// A device capability descriptor in the BOS.
   DeviceCapability = 16u,
};

/// Standard request codes (bRequest), USB 2.0 table 9-4.
enum class StandardRequest : uint8_t {
   GetStatus = 0u,
   ClearFeature = 1u,
   SetFeature = 3u,
   SetAddress = 5u,
   GetDescriptor = 6u,
   SetDescriptor = 7u,
   GetConfiguration = 8u,
   SetConfiguration = 9u,
   GetInterface = 10u,
   SetInterface = 11u,
   SynchFrame = 12u,
};

/// Standard feature selectors (wValue of CLEAR_FEATURE and SET_FEATURE), USB 2.0 table 9-6.
enum class FeatureSelector : uint16_t {
   EndpointHalt = 0u,
   DeviceRemoteWakeup = 1u,
   TestMode = 2u,
};

/// Direction of the data stage of a control transfer (bit 7 of bmRequestType).
enum class RequestDirection : uint8_t {
   HostToDevice = 0u,
   DeviceToHost = 1u,
};

/// Request type (bits 6..5 of bmRequestType).
enum class RequestType : uint8_t {
   Standard = 0u,
   Class = 1u,
   Vendor = 2u,
   Reserved = 3u,
};

/// Request recipient (bits 4..0 of bmRequestType).
enum class RequestRecipient : uint8_t {
   Device = 0u,
   Interface = 1u,
   Endpoint = 2u,
   Other = 3u,
};

/**
 * @brief The 8-byte SETUP packet that starts every control transfer (USB 2.0 section 9.3).
 *
 * The fields keep the names of the specification. The wire format is little-endian; fromBytes() converts it.
 */
struct SetupPacket {
   /// Size of a SETUP packet on the wire.
   static constexpr size_t kSize = 8u;

   uint8_t bmRequestType{ 0u };
   uint8_t bRequest{ 0u };
   uint16_t wValue{ 0u };
   uint16_t wIndex{ 0u };
   uint16_t wLength{ 0u };

   /// Decodes the wire format. Returns a packet of zeros if `bytes` is shorter than kSize.
   static constexpr SetupPacket fromBytes(art::span<const uint8_t> bytes) {
      SetupPacket packet{};
      if(bytes.size() < kSize) {
         return packet;
      }
      packet.bmRequestType = bytes[0];
      packet.bRequest = bytes[1];
      packet.wValue = static_cast<uint16_t>(bytes[2] | (bytes[3] << 8u));
      packet.wIndex = static_cast<uint16_t>(bytes[4] | (bytes[5] << 8u));
      packet.wLength = static_cast<uint16_t>(bytes[6] | (bytes[7] << 8u));
      return packet;
   }

   [[nodiscard]] constexpr RequestDirection direction() const {
      return (bmRequestType & 0x80u) != 0u ? RequestDirection::DeviceToHost : RequestDirection::HostToDevice;
   }

   [[nodiscard]] constexpr RequestType type() const {
      return static_cast<RequestType>((bmRequestType >> 5u) & 0x03u);
   }

   [[nodiscard]] constexpr RequestRecipient recipient() const {
      return static_cast<RequestRecipient>(bmRequestType & 0x1Fu);
   }

   /// True for a request of type Standard with the given code.
   [[nodiscard]] constexpr bool isStandard(StandardRequest request) const {
      return type() == RequestType::Standard && bRequest == static_cast<uint8_t>(request);
   }
};

/**
 * @brief Chapter 9 device state (USB 2.0 section 9.1.1).
 *
 * The device has no VBUS sense, so Attached and Powered are one state: Powered.
 */
enum class DeviceState : uint8_t {
   /// Not connected to the bus (pull-up off), Device::start() not called yet or Device::stop() called.
   Detached,
   /// Connected, waiting for the first bus reset.
   Powered,
   /// After a bus reset: address 0, not configured.
   Default,
   /// Has a non-zero address, not configured.
   Address,
   /// SET_CONFIGURATION with a non-zero value succeeded: the endpoints of the configuration are open.
   Configured,
   /// The bus was idle for 3 ms. Device::state() reports Suspended; resume returns to the state before.
   Suspended,
};

} // namespace art::usb
