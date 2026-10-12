#pragma once

#include "usb/DescriptorBuilder.h"
#include "usb/UsbTypes.h"
#include "usb/VendorRequestHandler.h"
#include "utils/compat/span.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * @file
 * @brief MS OS 2.0 descriptors: let Windows 8.1 and later bind a driver (normally WinUSB) to a device or to one
 * function of a composite device, with no INF file and no user action.
 *
 * Windows reads them in three steps:
 * 1. The device descriptor has bcdUSB 0x0201 or more, so Windows asks for the BOS descriptor.
 * 2. The BOS holds the MS OS 2.0 platform capability descriptor (platformCapability()). It gives the vendor code and
 *    the size of the descriptor set.
 * 3. Windows sends a vendor request to the device: bmRequestType 0xC0, bRequest the vendor code, wValue 0, wIndex 7
 *    (kDescriptorIndex). The reply is the descriptor set (descriptorSet()). DescriptorSetHandler answers it.
 *
 * Example for a composite device whose interface 0 uses WinUSB:
 *
 * @code
 * using namespace art::usb;
 * constexpr auto kMsOs20Set = msos20::descriptorSet(msos20::kWindows81,
 *    msos20::functionSubset(0u, msos20::compatibleId("WINUSB"),
 *       msos20::deviceInterfaceGuid(u"{CDB3B5AD-293B-4663-AA36-1AAE46463776}")));
 * constexpr auto kBos = descriptor::bos(msos20::platformCapability(kVendorCode, kMsOs20Set));
 * msos20::DescriptorSetHandler handler(kVendorCode, descriptor::asSpan(kMsOs20Set));
 * device.setVendorRequestHandler(&handler);
 * @endcode
 *
 * A device with one configuration puts the function subsets directly after the set header, as above (DAPLink does
 * the same). A device that is not composite leaves out functionSubset() too: the feature descriptors then apply to
 * the whole device. The builders check their
 * input as the builders of usb/DescriptorBuilder.h do (see "Checks" there).
 *
 * Windows keeps data about the descriptors of a device in the registry key
 * HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<vvvvpppprrrr> (VID, PID, bcdDevice). A driver that was installed
 * for the device before (for example with Zadig) has priority over the WinUSB compatible ID. After a change of these
 * descriptors, uninstall the device and its driver in the Device Manager and delete that key.
 *
 * Reference: Microsoft, "Microsoft OS 2.0 Descriptors Specification", July 2018.
 */

namespace art::usb::msos20 {

/// dwWindowsVersion of Windows 8.1, the first version that reads MS OS 2.0 descriptors.
constexpr uint32_t kWindows81 = 0x06030000u;
/// wIndex of the vendor request that reads the descriptor set (MS_OS_20_DESCRIPTOR_INDEX).
constexpr uint16_t kDescriptorIndex = 7u;
/// wIndex of the vendor request that selects an alternate enumeration (MS_OS_20_SET_ALT_ENUMERATION). The
/// builders set bAltEnumCode 0, so Windows does not send it.
constexpr uint16_t kSetAltEnumerationIndex = 8u;

/// wDescriptorType of the MS OS 2.0 descriptors (specification table 9).
enum class DescriptorType : uint16_t {
   SetHeader = 0u,
   ConfigurationSubsetHeader = 1u,
   FunctionSubsetHeader = 2u,
   CompatibleId = 3u,
   RegistryProperty = 4u,
};

/// wPropertyDataType of a registry property: REG_MULTI_SZ, a list of null-terminated UTF-16 strings.
constexpr uint16_t kRegistryMultiString = 7u;

namespace detail {

using descriptor::detail::highByte;
using descriptor::detail::lowByte;
using descriptor::detail::malformedDescriptorSequence;

// Not constexpr, see "Checks" in usb/DescriptorBuilder.h.
[[noreturn]] inline void compatibleIdNeedsNullTerminatedLiteral() noexcept {
   __builtin_trap();
}

[[noreturn]] inline void deviceInterfaceGuidNeedsBracedLiteral() noexcept {
   __builtin_trap();
}

constexpr uint16_t readWord(const uint8_t *bytes) {
   return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8u));
}

/// An 8-byte ID field (CompatibleID, SubCompatibleID) from a string literal of at most 8 ASCII characters, padded
/// with zeros.
template <size_t Length>
constexpr std::array<uint8_t, 8> idField(const char (&text)[Length]) {
   static_assert(Length >= 1u && Length <= 9u, "an MS OS 2.0 compatible ID has at most 8 characters");
   if(text[Length - 1u] != '\0') {
      compatibleIdNeedsNullTerminatedLiteral();
   }
   std::array<uint8_t, 8> field{};
   for(size_t index = 0u; index + 1u < Length; ++index) {
      field[index] = static_cast<uint8_t>(text[index]);
   }
   return field;
}

/// Checks that the bytes from `offset` are whole MS OS 2.0 descriptors: each wLength at least 4, the last one ends at
/// the end of the array. A subset header covers only its own 8 bytes, so the walk goes into the subset.
template <size_t Size>
constexpr void checkSequence(const std::array<uint8_t, Size> &descriptors, size_t offset) {
   while(offset + 4u <= Size) {
      const uint16_t length = readWord(&descriptors[offset]);
      if(length < 4u) {
         break;
      }
      offset += length;
   }
   if(offset != Size) {
      malformedDescriptorSequence();
   }
}

/// A subset header (configuration or function) followed by its descriptors.
template <size_t... Sizes>
constexpr std::array<uint8_t, (8u + ... + Sizes)>
subset(DescriptorType type, uint8_t value, const std::array<uint8_t, Sizes> &...parts) {
   constexpr size_t totalLength = (8u + ... + Sizes);
   static_assert(totalLength <= 0xFFFFu, "the subset does not fit into 16 bits");
   const std::array<uint8_t, 8> header{
      8u,    0u, lowByte(static_cast<uint16_t>(type)),        highByte(static_cast<uint16_t>(type)),
      value, 0u, lowByte(static_cast<uint16_t>(totalLength)), highByte(static_cast<uint16_t>(totalLength))
   };
   std::array<uint8_t, totalLength> result = descriptor::concat(header, parts...);
   checkSequence(result, 8u);
   return result;
}

} // namespace detail

/**
 * @brief The descriptor set: the set header followed by the subsets or feature descriptors. DescriptorSetHandler
 * sends it.
 *
 * wTotalLength is the size of the result. Parts that are not a sequence of whole MS OS 2.0 descriptors fail the build.
 *
 * @param windowsVersion dwWindowsVersion: the lowest Windows version the set applies to, normally kWindows81.
 */
template <size_t... Sizes>
constexpr std::array<uint8_t, (10u + ... + Sizes)>
descriptorSet(uint32_t windowsVersion, const std::array<uint8_t, Sizes> &...parts) {
   constexpr size_t totalLength = (10u + ... + Sizes);
   static_assert(totalLength <= 0xFFFFu, "wTotalLength does not fit into 16 bits");
   const std::array<uint8_t, 10> header{ 10u,
                                         0u,
                                         detail::lowByte(static_cast<uint16_t>(DescriptorType::SetHeader)),
                                         detail::highByte(static_cast<uint16_t>(DescriptorType::SetHeader)),
                                         static_cast<uint8_t>(windowsVersion & 0xFFu),
                                         static_cast<uint8_t>((windowsVersion >> 8u) & 0xFFu),
                                         static_cast<uint8_t>((windowsVersion >> 16u) & 0xFFu),
                                         static_cast<uint8_t>((windowsVersion >> 24u) & 0xFFu),
                                         detail::lowByte(static_cast<uint16_t>(totalLength)),
                                         detail::highByte(static_cast<uint16_t>(totalLength)) };
   std::array<uint8_t, totalLength> result = descriptor::concat(header, parts...);
   detail::checkSequence(result, 10u);
   return result;
}

/**
 * @brief The configuration subset header followed by the function subsets of one configuration.
 *
 * Only a device with more than one configuration needs it. libArT devices have one configuration
 * (usb::Device), so they put the function subsets directly into descriptorSet().
 *
 * @param configurationIndex The zero-based index of the configuration. The specification names the field
 * bConfigurationValue, but Windows reads it as the index, and TinyUSB and other stacks fill it so (0 for the first
 * configuration). This is not tested on Windows here, because usb::Device has one configuration.
 */
template <size_t... Sizes>
constexpr std::array<uint8_t, (8u + ... + Sizes)>
configurationSubset(uint8_t configurationIndex, const std::array<uint8_t, Sizes> &...parts) {
   return detail::subset(DescriptorType::ConfigurationSubsetHeader, configurationIndex, parts...);
}

/**
 * @brief The function subset header followed by the feature descriptors of one function of a composite device.
 *
 * @param firstInterface bFirstInterface: the first interface of the function. For a function with one interface,
 * that interface.
 */
template <size_t... Sizes>
constexpr std::array<uint8_t, (8u + ... + Sizes)>
functionSubset(uint8_t firstInterface, const std::array<uint8_t, Sizes> &...parts) {
   return detail::subset(DescriptorType::FunctionSubsetHeader, firstInterface, parts...);
}

/**
 * @brief The compatible ID descriptor, for example `compatibleId("WINUSB")`: Windows loads the driver of this
 * compatible ID (winusb.inf for "WINUSB").
 *
 * Both IDs are string literals of at most 8 ASCII characters; a longer one fails the build.
 */
template <size_t Length, size_t SubLength = 1u>
constexpr std::array<uint8_t, 20> compatibleId(const char (&id)[Length], const char (&subCompatibleId)[SubLength] = "") {
   const std::array<uint8_t, 8> idBytes = detail::idField(id);
   const std::array<uint8_t, 8> subIdBytes = detail::idField(subCompatibleId);
   const std::array<uint8_t, 4> header{ 20u, 0u, detail::lowByte(static_cast<uint16_t>(DescriptorType::CompatibleId)),
                                        detail::highByte(static_cast<uint16_t>(DescriptorType::CompatibleId)) };
   return descriptor::concat(header, idBytes, subIdBytes);
}

/// The size of the registry property descriptor of deviceInterfaceGuid().
constexpr size_t kDeviceInterfaceGuidSize = 132u;

/**
 * @brief The registry property descriptor "DeviceInterfaceGUIDs" (REG_MULTI_SZ) with one GUID, for example
 * `deviceInterfaceGuid(u"{CDB3B5AD-293B-4663-AA36-1AAE46463776}")`.
 *
 * WinUSB registers the device interface of the function with this GUID. libusb and other WinUSB clients need the
 * property to find the device. The GUID is a literal in braces, 38 characters; anything else fails the build.
 */
constexpr std::array<uint8_t, kDeviceInterfaceGuidSize> deviceInterfaceGuid(const char16_t (&guid)[39]) {
   constexpr char16_t kName[] = u"DeviceInterfaceGUIDs";
   constexpr size_t kNameSize = sizeof(kName);
   // The GUID, its null, and the null that ends the list.
   constexpr size_t kDataSize = 2u * (38u + 2u);
   static_assert(10u + kNameSize + kDataSize == kDeviceInterfaceGuidSize, "layout of the registry property");

   if(guid[0] != u'{' || guid[37] != u'}' || guid[38] != u'\0') {
      detail::deviceInterfaceGuidNeedsBracedLiteral();
   }
   std::array<uint8_t, kDeviceInterfaceGuidSize> result{};
   size_t offset = 0u;
   auto appendWord = [&result, &offset](uint16_t value) {
      result[offset++] = detail::lowByte(value);
      result[offset++] = detail::highByte(value);
   };
   appendWord(static_cast<uint16_t>(kDeviceInterfaceGuidSize));
   appendWord(static_cast<uint16_t>(DescriptorType::RegistryProperty));
   appendWord(kRegistryMultiString);
   appendWord(static_cast<uint16_t>(kNameSize));
   for(const char16_t character : kName) {
      appendWord(static_cast<uint16_t>(character));
   }
   appendWord(static_cast<uint16_t>(kDataSize));
   for(const char16_t character : guid) {
      appendWord(static_cast<uint16_t>(character));
   }
   // The second null ends the REG_MULTI_SZ list; the array is zero-initialized.
   return result;
}

/**
 * @brief The MS OS 2.0 platform capability descriptor, for descriptor::bos().
 *
 * It takes dwWindowsVersion and wMSOSDescriptorSetTotalLength from `descriptorSet`, so they cannot differ from the
 * set that DescriptorSetHandler sends. An array that does not start with a set header fails the build.
 *
 * @param vendorCode bMS_VendorCode: the bRequest of the vendor request that reads the set. It must not be used by
 * another vendor request of the device.
 */
template <size_t SetSize>
constexpr std::array<uint8_t, 28> platformCapability(uint8_t vendorCode, const std::array<uint8_t, SetSize> &descriptorSet) {
   static_assert(SetSize >= 10u, "expects a descriptor set from descriptorSet()");
   if(detail::readWord(&descriptorSet[0]) != 10u ||
      detail::readWord(&descriptorSet[2]) != static_cast<uint16_t>(DescriptorType::SetHeader) ||
      detail::readWord(&descriptorSet[8]) != SetSize) {
      detail::malformedDescriptorSequence();
   }
   return { 28u, static_cast<uint8_t>(usb::DescriptorType::DeviceCapability),
            // bDevCapabilityType: PLATFORM.
            0x05u, 0u,
            // PlatformCapabilityUUID {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F}, in the byte order of a GUID.
            0xDFu, 0x60u, 0xDDu, 0xD8u, 0x89u, 0x45u, 0xC7u, 0x4Cu, 0x9Cu, 0xD2u, 0x65u, 0x9Du, 0x9Eu, 0x64u, 0x8Au, 0x9Fu,
            // dwWindowsVersion.
            descriptorSet[4], descriptorSet[5], descriptorSet[6], descriptorSet[7],
            // wMSOSDescriptorSetTotalLength.
            descriptorSet[8], descriptorSet[9], vendorCode,
            // bAltEnumCode: no alternate enumeration.
            0u };
}

/**
 * @brief Answers the request of Windows for the MS OS 2.0 descriptor set. Register it with
 * Device::setVendorRequestHandler().
 *
 * It answers a device-to-host request with bRequest `vendorCode`, wValue 0 and wIndex kDescriptorIndex, and stalls
 * every other request. The set must stay valid for the lifetime of the handler (normally a `constexpr` array in
 * flash).
 */
class DescriptorSetHandler final : public VendorRequestHandler {
   public:
   DescriptorSetHandler(uint8_t vendorCode, art::span<const uint8_t> descriptorSet)
      : vendorCode_(vendorCode)
      , descriptorSet_(descriptorSet) {
   }

   std::optional<art::span<const uint8_t>> vendorRequest(const SetupPacket &setup, art::span<uint8_t> buffer) override {
      static_cast<void>(buffer);
      if(setup.direction() != RequestDirection::DeviceToHost || setup.bRequest != vendorCode_ || setup.wValue != 0u ||
         setup.wIndex != kDescriptorIndex || descriptorSet_.empty()) {
         return std::nullopt;
      }
      return descriptorSet_;
   }

   private:
   uint8_t vendorCode_;
   art::span<const uint8_t> descriptorSet_;
};

} // namespace art::usb::msos20
