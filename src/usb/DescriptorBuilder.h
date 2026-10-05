#pragma once

#include "usb/UsbTypes.h"
#include "utils/compat/span.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/**
 * @file
 * @brief `constexpr` builders for the standard and CDC descriptors of a USB device.
 *
 * Each builder returns a `std::array<uint8_t, N>` with the descriptor in wire format (little-endian). The builders
 * that contain other descriptors (interfaceDescriptor(), configuration()) take them as arguments, concatenate them and
 * compute the derived fields: bNumEndpoints, bNumInterfaces and wTotalLength. A firmware builds its descriptors at
 * compile time, in flash:
 *
 * @code
 * using namespace art::usb::descriptor;
 * constexpr auto kConfiguration = configuration(ConfigurationFields{ ... },
 *    interfaceAssociation(0, 2, classcode::kCdc, cdc::kSubclassAcm, 0),
 *    interfaceDescriptor(InterfaceFields{ 0, 0, classcode::kCdc, cdc::kSubclassAcm, 0, 0 },
 *       cdcHeader(), cdcCallManagement(0, 1), cdcAcm(), cdcUnion(0, 1),
 *       endpoint(0x81, EndpointType::Interrupt, 16, 16)),
 *    ...);
 * @endcode
 *
 * @par Checks
 * Sizes that must fit into a length field are checked with `static_assert`. Some values are checked too: a maximum
 * power above 500 mA, a string that is not a null-terminated literal, and a malformed descriptor sequence in
 * configuration() or interfaceDescriptor(). Such a value calls a function of detail:: that is declared but neither
 * `constexpr` nor defined, so a constant evaluation fails with a compile error that names the problem.
 *
 * Evaluate the builders at compile time only, in `constexpr` variables. A builder with such a check that is called
 * at run time references the undefined function. The program then does not link, also if the values are valid,
 * unless the optimizer removes the check. Other
 * field values are not checked: the descriptor carries what the caller passes.
 */

namespace art::usb::descriptor {

/// Class codes (bDeviceClass, bInterfaceClass) of usb.org, "Defined Class Codes".
namespace classcode {
/// The class is defined per interface (bDeviceClass only).
constexpr uint8_t kPerInterface = 0x00u;
constexpr uint8_t kCdc = 0x02u;
constexpr uint8_t kMassStorage = 0x08u;
constexpr uint8_t kCdcData = 0x0Au;
/// Miscellaneous. With subclass kMiscellaneousSubclassCommon and protocol kMiscellaneousProtocolIad, the device
/// descriptor of a composite device with interface association descriptors.
constexpr uint8_t kMiscellaneous = 0xEFu;
constexpr uint8_t kMiscellaneousSubclassCommon = 0x02u;
constexpr uint8_t kMiscellaneousProtocolIad = 0x01u;
} // namespace classcode

/// Codes of the CDC 1.20 specification and its PSTN subclass specification.
namespace cdc {
/// Subclass of the communication interface: Abstract Control Model.
constexpr uint8_t kSubclassAcm = 0x02u;
/// bDescriptorType of the functional descriptors: CS_INTERFACE.
constexpr uint8_t kCsInterface = 0x24u;
// bDescriptorSubtype of the functional descriptors.
constexpr uint8_t kSubtypeHeader = 0x00u;
constexpr uint8_t kSubtypeCallManagement = 0x01u;
constexpr uint8_t kSubtypeAcm = 0x02u;
constexpr uint8_t kSubtypeUnion = 0x06u;
/// bmCapabilities of the ACM descriptor: SET_LINE_CODING, GET_LINE_CODING, SET_CONTROL_LINE_STATE and the
/// SERIAL_STATE notification are supported.
constexpr uint8_t kAcmLineCoding = 0x02u;
} // namespace cdc

/// LANGID of US English, the language that hosts ask for first.
constexpr uint16_t kLanguageEnglishUs = 0x0409u;

/// Fields of the device descriptor (USB 2.0 table 9-8). The defaults describe a USB 2.0 full-speed device with an
/// EP0 packet size of 64 and one configuration, whose class is defined per interface.
struct DeviceFields {
   uint16_t bcdUSB{ 0x0200u };
   uint8_t bDeviceClass{ classcode::kPerInterface };
   uint8_t bDeviceSubClass{ 0u };
   uint8_t bDeviceProtocol{ 0u };
   uint8_t bMaxPacketSize0{ 64u };
   uint16_t idVendor{ 0u };
   uint16_t idProduct{ 0u };
   uint16_t bcdDevice{ 0u };
   uint8_t iManufacturer{ 0u };
   uint8_t iProduct{ 0u };
   uint8_t iSerialNumber{ 0u };
   uint8_t bNumConfigurations{ 1u };
};

/// Fields of the configuration descriptor (USB 2.0 table 9-10) that configuration() does not compute.
struct ConfigurationFields {
   uint8_t bConfigurationValue{ 1u };
   uint8_t iConfiguration{ 0u };
   /// bmAttributes bit 6.
   bool selfPowered{ false };
   /// bmAttributes bit 5.
   bool remoteWakeup{ false };
   /// The maximum current from the bus in mA, at most 500 (a larger value fails the build). bMaxPower is in units of
   /// 2 mA, so an odd value is rounded up.
   uint16_t maxPowerMilliamps{ 100u };
};

/// Fields of the interface descriptor (USB 2.0 table 9-12) that interfaceDescriptor() does not compute.
struct InterfaceFields {
   uint8_t bInterfaceNumber{ 0u };
   uint8_t bAlternateSetting{ 0u };
   uint8_t bInterfaceClass{ 0u };
   uint8_t bInterfaceSubClass{ 0u };
   uint8_t bInterfaceProtocol{ 0u };
   uint8_t iInterface{ 0u };
};

namespace detail {

// Declared, never defined, and not constexpr: calling one of them makes a constant evaluation fail with an error that
// names it, and a run-time call fails to link. See "Checks" above.
void maxPowerAbove500Milliamps();
void stringDescriptorNeedsNullTerminatedLiteral();
void malformedDescriptorSequence();

constexpr uint8_t lowByte(uint16_t value) {
   return static_cast<uint8_t>(value & 0xFFu);
}

constexpr uint8_t highByte(uint16_t value) {
   return static_cast<uint8_t>(value >> 8u);
}

/// Counts the descriptors of a type in a sequence of descriptors. With `alternateSettingZero`, it counts only
/// interface descriptors of alternate setting 0, that is, the interfaces. The sequence from `offset` must consist of
/// whole descriptors (bLength at least 2, the last one ends at the end of the array); otherwise the build fails.
template <size_t Size>
constexpr uint8_t
countDescriptors(const std::array<uint8_t, Size> &descriptors, size_t offset, DescriptorType type, bool alternateSettingZero = false) {
   uint8_t count = 0u;
   while(offset + 2u <= Size && descriptors[offset] >= 2u) {
      if(descriptors[offset + 1u] == static_cast<uint8_t>(type) &&
         (!alternateSettingZero || (offset + 3u < Size && descriptors[offset + 3u] == 0u))) {
         ++count;
      }
      offset += descriptors[offset];
   }
   if(offset != Size) {
      malformedDescriptorSequence();
   }
   return count;
}

} // namespace detail

/// Concatenates descriptors (or any byte arrays) into one array.
template <size_t... Sizes>
constexpr std::array<uint8_t, (0u + ... + Sizes)> concat(const std::array<uint8_t, Sizes> &...parts) {
   std::array<uint8_t, (0u + ... + Sizes)> result{};
   size_t offset = 0u;
   auto append = [&result, &offset](const auto &part) {
      for(size_t index = 0u; index < part.size(); ++index) {
         result[offset + index] = part[index];
      }
      offset += part.size();
   };
   (append(parts), ...);
   return result;
}

/// The 18-byte device descriptor.
constexpr std::array<uint8_t, 18> device(const DeviceFields &fields) {
   return { 18u,
            static_cast<uint8_t>(DescriptorType::Device),
            detail::lowByte(fields.bcdUSB),
            detail::highByte(fields.bcdUSB),
            fields.bDeviceClass,
            fields.bDeviceSubClass,
            fields.bDeviceProtocol,
            fields.bMaxPacketSize0,
            detail::lowByte(fields.idVendor),
            detail::highByte(fields.idVendor),
            detail::lowByte(fields.idProduct),
            detail::highByte(fields.idProduct),
            detail::lowByte(fields.bcdDevice),
            detail::highByte(fields.bcdDevice),
            fields.iManufacturer,
            fields.iProduct,
            fields.iSerialNumber,
            fields.bNumConfigurations };
}

/**
 * @brief The configuration descriptor followed by the descriptors of its interfaces (the full set that GET_DESCRIPTOR
 * of the configuration returns).
 *
 * wTotalLength is the size of the result. bNumInterfaces is the number of interface descriptors with alternate
 * setting 0 in `parts`. bmAttributes has bit 7 set, as USB 2.0 requires.
 */
template <size_t... Sizes>
constexpr std::array<uint8_t, (9u + ... + Sizes)>
configuration(const ConfigurationFields &fields, const std::array<uint8_t, Sizes> &...parts) {
   constexpr size_t totalLength = (9u + ... + Sizes);
   static_assert(totalLength <= 0xFFFFu, "wTotalLength does not fit into 16 bits");
   if(fields.maxPowerMilliamps > 500u) {
      // USB 2.0 allows at most 500 mA (bMaxPower 250).
      detail::maxPowerAbove500Milliamps();
   }
   const std::array<uint8_t, 9> header{ 9u,
                                        static_cast<uint8_t>(DescriptorType::Configuration),
                                        detail::lowByte(static_cast<uint16_t>(totalLength)),
                                        detail::highByte(static_cast<uint16_t>(totalLength)),
                                        0u,
                                        fields.bConfigurationValue,
                                        fields.iConfiguration,
                                        static_cast<uint8_t>(0x80u | (fields.selfPowered ? 0x40u : 0u) |
                                                             (fields.remoteWakeup ? 0x20u : 0u)),
                                        static_cast<uint8_t>((fields.maxPowerMilliamps + 1u) / 2u) };
   std::array<uint8_t, totalLength> result = concat(header, parts...);
   result[4] = detail::countDescriptors(result, 9u, DescriptorType::Interface, true);
   return result;
}

/// The interface association descriptor (IAD, USB 2.0 ECN "Interface Association Descriptors").
constexpr std::array<uint8_t, 8> interfaceAssociation(uint8_t firstInterface,
                                                      uint8_t interfaceCount,
                                                      uint8_t functionClass,
                                                      uint8_t functionSubClass,
                                                      uint8_t functionProtocol,
                                                      uint8_t iFunction = 0u) {
   return {
      8u, static_cast<uint8_t>(DescriptorType::InterfaceAssociation), firstInterface, interfaceCount, functionClass, functionSubClass, functionProtocol, iFunction
   };
}

/**
 * @brief The interface descriptor followed by the class-specific and endpoint descriptors of the interface.
 *
 * bNumEndpoints is the number of endpoint descriptors in `parts`. (The name is not `interface()`, because the Windows
 * headers define `interface` as a macro.)
 */
template <size_t... Sizes>
constexpr std::array<uint8_t, (9u + ... + Sizes)>
interfaceDescriptor(const InterfaceFields &fields, const std::array<uint8_t, Sizes> &...parts) {
   const std::array<uint8_t, 9> header{ 9u,
                                        static_cast<uint8_t>(DescriptorType::Interface),
                                        fields.bInterfaceNumber,
                                        fields.bAlternateSetting,
                                        0u,
                                        fields.bInterfaceClass,
                                        fields.bInterfaceSubClass,
                                        fields.bInterfaceProtocol,
                                        fields.iInterface };
   std::array<uint8_t, (9u + ... + Sizes)> result = concat(header, parts...);
   result[4] = detail::countDescriptors(result, 9u, DescriptorType::Endpoint);
   return result;
}

/**
 * @brief The endpoint descriptor (USB 2.0 table 9-13).
 *
 * @param address Endpoint number in bits 3..0, bit 7 set for IN.
 * @param interval bInterval: the polling interval in ms for interrupt endpoints at full speed; 0 for bulk.
 */
constexpr std::array<uint8_t, 7> endpoint(uint8_t address, EndpointType type, uint16_t maxPacketSize, uint8_t interval = 0u) {
   return { 7u,
            static_cast<uint8_t>(DescriptorType::Endpoint),
            address,
            static_cast<uint8_t>(type),
            detail::lowByte(maxPacketSize),
            detail::highByte(maxPacketSize),
            interval };
}

/// The CDC header functional descriptor. `bcdCDC` is the CDC release (default 1.20).
constexpr std::array<uint8_t, 5> cdcHeader(uint16_t bcdCDC = 0x0120u) {
   return { 5u, cdc::kCsInterface, cdc::kSubtypeHeader, detail::lowByte(bcdCDC), detail::highByte(bcdCDC) };
}

/// The CDC call management functional descriptor (PSTN 1.20 section 5.3.1).
constexpr std::array<uint8_t, 5> cdcCallManagement(uint8_t capabilities, uint8_t dataInterface) {
   return { 5u, cdc::kCsInterface, cdc::kSubtypeCallManagement, capabilities, dataInterface };
}

/// The CDC abstract control management functional descriptor (PSTN 1.20 section 5.3.2).
constexpr std::array<uint8_t, 4> cdcAcm(uint8_t capabilities = cdc::kAcmLineCoding) {
   return { 4u, cdc::kCsInterface, cdc::kSubtypeAcm, capabilities };
}

/// The CDC union functional descriptor (CDC 1.20 section 5.2.3.2): the control interface and its subordinate
/// interfaces.
template <typename... Subordinates>
constexpr std::array<uint8_t, 4u + sizeof...(Subordinates)> cdcUnion(uint8_t controlInterface, Subordinates... subordinateInterfaces) {
   static_assert(sizeof...(Subordinates) >= 1u, "a union needs at least one subordinate interface");
   return { static_cast<uint8_t>(4u + sizeof...(Subordinates)), cdc::kCsInterface, cdc::kSubtypeUnion, controlInterface,
            static_cast<uint8_t>(subordinateInterfaces)... };
}

/// String descriptor 0: the LANGIDs that the string descriptors support.
template <typename... LanguageIds>
constexpr std::array<uint8_t, 2u + 2u * sizeof...(LanguageIds)> languages(LanguageIds... languageIds) {
   static_assert(sizeof...(LanguageIds) >= 1u, "string descriptor 0 needs at least one LANGID");
   static_assert(2u + 2u * sizeof...(LanguageIds) <= 255u, "too many LANGIDs for one descriptor");
   const std::array<uint16_t, sizeof...(LanguageIds)> ids{ static_cast<uint16_t>(languageIds)... };
   std::array<uint8_t, 2u + 2u * sizeof...(LanguageIds)> result{};
   result[0] = static_cast<uint8_t>(result.size());
   result[1] = static_cast<uint8_t>(DescriptorType::String);
   for(size_t index = 0u; index < ids.size(); ++index) {
      result[2u + 2u * index] = detail::lowByte(ids[index]);
      result[3u + 2u * index] = detail::highByte(ids[index]);
   }
   return result;
}

/**
 * @brief A string descriptor from a UTF-16 string literal, for example `stringDescriptor(u"Oven controller")`.
 *
 * The terminating null is not part of the descriptor. An array whose last element is not a null (not a literal) fails
 * the build, so that no character is lost. A string has at most 126 UTF-16 code units, the limit of the one-byte
 * bLength. Characters outside the basic multilingual plane need two code units (a surrogate pair), as the
 * literal stores them.
 */
template <size_t Length>
constexpr std::array<uint8_t, 2u * Length> stringDescriptor(const char16_t (&text)[Length]) {
   static_assert(Length >= 1u, "expects a string literal");
   static_assert(2u * Length <= 255u, "a string descriptor has at most 126 UTF-16 code units");
   if(text[Length - 1u] != u'\0') {
      detail::stringDescriptorNeedsNullTerminatedLiteral();
   }
   std::array<uint8_t, 2u * Length> result{};
   result[0] = static_cast<uint8_t>(2u * Length);
   result[1] = static_cast<uint8_t>(DescriptorType::String);
   for(size_t index = 0u; index + 1u < Length; ++index) {
      result[2u + 2u * index] = detail::lowByte(static_cast<uint16_t>(text[index]));
      result[3u + 2u * index] = detail::highByte(static_cast<uint16_t>(text[index]));
   }
   return result;
}

/**
 * @brief A serial number string descriptor that is filled at run time from a byte sequence, such as the unique ID
 * of the MCU.
 *
 * Each byte becomes two upper-case hexadecimal digits, in the order of the input. Use a string of hexadecimal digits
 * only: Windows uses the serial number as part of a device instance ID and rejects some other characters.
 *
 * Call set() before Device::start(). usb::Descriptors::strings references the buffer of the object, and the host can
 * read the string at any time after the device connects.
 *
 * @tparam IdBytes The number of bytes of the ID (12 for the STM32 UID). The descriptor has 2 + 4 * IdBytes bytes.
 */
template <size_t IdBytes>
class SerialNumber {
   public:
   static_assert(IdBytes >= 1u && 2u + 4u * IdBytes <= 255u, "the ID must have 1 to 63 bytes");

   /// The size of the descriptor in bytes.
   static constexpr size_t kSize = 2u + 4u * IdBytes;

   /// A descriptor of IdBytes zero bytes ("00...0"), until set() is called.
   constexpr SerialNumber() {
      descriptor_[0] = static_cast<uint8_t>(kSize);
      descriptor_[1] = static_cast<uint8_t>(DescriptorType::String);
      for(size_t index = 2u; index < kSize; index += 2u) {
         descriptor_[index] = '0';
      }
   }

   /**
    * @brief Sets the digits from `id`, an array or a span of exactly IdBytes bytes.
    *
    * An ID of another length does not compile, and neither does a span of dynamic extent (use `first<IdBytes>()`
    * when the length is checked). The overloads check the length themselves, because the fixed-extent etl::span
    * (ART_USE_STD_EXPECTED_SPAN=0) converts from containers and spans of any length.
    */
   constexpr void set(const std::array<uint8_t, IdBytes> &id) {
      setDigits(id.data());
   }

   /// @copydoc set(const std::array<uint8_t, IdBytes> &)
   constexpr void set(const uint8_t (&id)[IdBytes]) {
      setDigits(id);
   }

   /// @copydoc set(const std::array<uint8_t, IdBytes> &)
   template <typename Byte, size_t Extent, typename std::enable_if<Extent == IdBytes && std::is_same<typename std::remove_const<Byte>::type, uint8_t>::value, int>::type = 0>
   constexpr void set(art::span<Byte, Extent> id) {
      setDigits(id.data());
   }

   /// The descriptor, for usb::Descriptors::strings. The span stays valid as long as the object.
   [[nodiscard]] art::span<const uint8_t> bytes() const {
      return art::span<const uint8_t>(descriptor_.data(), descriptor_.size());
   }

   private:
   constexpr void setDigits(const uint8_t *id) {
      for(size_t index = 0u; index < IdBytes; ++index) {
         descriptor_[2u + 4u * index] = hexDigit(static_cast<uint8_t>(id[index] >> 4u));
         descriptor_[4u + 4u * index] = hexDigit(static_cast<uint8_t>(id[index] & 0x0Fu));
      }
   }

   static constexpr uint8_t hexDigit(uint8_t value) {
      return static_cast<uint8_t>(value < 10u ? '0' + value : 'A' + (value - 10u));
   }

   // The high bytes of the UTF-16 code units stay 0.
   std::array<uint8_t, kSize> descriptor_{};
};

/// A span over a built descriptor, for the fields of usb::Descriptors.
template <size_t Size>
constexpr art::span<const uint8_t> asSpan(const std::array<uint8_t, Size> &descriptor) {
   return art::span<const uint8_t>(descriptor.data(), descriptor.size());
}

} // namespace art::usb::descriptor
