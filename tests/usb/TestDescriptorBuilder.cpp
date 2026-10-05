#include "FakeDeviceController.h"
#include "usb/DescriptorBuilder.h"
#include "usb/Device.h"
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <type_traits>
#include <utility>
#include <vector>

using namespace art::usb;
using namespace art::usb::descriptor;

namespace {

template <size_t Size>
std::vector<uint8_t> bytesOf(const std::array<uint8_t, Size> &descriptor) {
   return std::vector<uint8_t>(descriptor.begin(), descriptor.end());
}

// The reference: the descriptors of the TinyUSB 0.16.0 example "cdc_msc"
// (examples/device/cdc_msc/src/usb_descriptors.c), a CDC-ACM + mass storage composite device, as the
// TUD_CONFIG_DESCRIPTOR, TUD_CDC_DESCRIPTOR and TUD_MSC_DESCRIPTOR macros of TinyUSB 0.16.0 (src/device/usbd.h)
// expand them. Interfaces: 0 CDC control (string 4), 1 CDC data, 2 mass storage (string 5). Endpoints: 0x81
// notification (8 bytes), 0x02/0x82 CDC data, 0x03/0x83 mass storage (64 bytes).
// Do not update these bytes to a later TinyUSB: its master expands differently (ACM bmCapabilities 0x06, a 16-byte
// notification endpoint with bInterval 1, PID 0x4007). ACM bmCapabilities 0x02 (no SEND_BREAK) is the intended
// library default.
// clang-format off
const std::vector<uint8_t> kReferenceDevice{
   0x12, 0x01, 0x00, 0x02, 0xEF, 0x02, 0x01, 0x40, 0xFE, 0xCA, 0x03, 0x40, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01,
};

const std::vector<uint8_t> kReferenceConfiguration{
   // TUD_CONFIG_DESCRIPTOR(1, 3, 0, 98, 0x00, 100)
   0x09, 0x02, 0x62, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,
   // TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64)
   0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00,
   0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x04,
   0x05, 0x24, 0x00, 0x20, 0x01,
   0x05, 0x24, 0x01, 0x00, 0x01,
   0x04, 0x24, 0x02, 0x02,
   0x05, 0x24, 0x06, 0x00, 0x01,
   0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x10,
   0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
   0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
   0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,
   // TUD_MSC_DESCRIPTOR(2, 5, 0x03, 0x83, 64)
   0x09, 0x04, 0x02, 0x00, 0x02, 0x08, 0x06, 0x50, 0x05,
   0x07, 0x05, 0x03, 0x02, 0x40, 0x00, 0x00,
   0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,
};
// clang-format on

constexpr DeviceFields makeDeviceFields() {
   DeviceFields fields{};
   fields.bDeviceClass = classcode::kMiscellaneous;
   fields.bDeviceSubClass = classcode::kMiscellaneousSubclassCommon;
   fields.bDeviceProtocol = classcode::kMiscellaneousProtocolIad;
   fields.idVendor = 0xCAFEu;
   fields.idProduct = 0x4003u;
   fields.bcdDevice = 0x0100u;
   fields.iManufacturer = 1u;
   fields.iProduct = 2u;
   fields.iSerialNumber = 3u;
   return fields;
}

constexpr ConfigurationFields kConfigurationFields{};

constexpr auto kDevice = device(makeDeviceFields());

constexpr auto kConfiguration =
configuration(kConfigurationFields,
              interfaceAssociation(0u, 2u, classcode::kCdc, cdc::kSubclassAcm, 0u),
              interfaceDescriptor(InterfaceFields{ 0u, 0u, classcode::kCdc, cdc::kSubclassAcm, 0u, 4u },
                                  cdcHeader(),
                                  cdcCallManagement(0u, 1u),
                                  cdcAcm(),
                                  cdcUnion(0u, 1u),
                                  endpoint(0x81u, EndpointType::Interrupt, 8u, 16u)),
              interfaceDescriptor(InterfaceFields{ 1u, 0u, classcode::kCdcData, 0u, 0u, 0u },
                                  endpoint(0x02u, EndpointType::Bulk, 64u),
                                  endpoint(0x82u, EndpointType::Bulk, 64u)),
              interfaceDescriptor(InterfaceFields{ 2u, 0u, classcode::kMassStorage, 0x06u, 0x50u, 5u },
                                  endpoint(0x03u, EndpointType::Bulk, 64u),
                                  endpoint(0x83u, EndpointType::Bulk, 64u)));

// The builders run at compile time.
static_assert(kConfiguration.size() == 98u, "wTotalLength of the reference");
static_assert(kConfiguration[2] == 98u && kConfiguration[3] == 0u, "wTotalLength is computed");
static_assert(kConfiguration[4] == 3u, "bNumInterfaces is computed");

constexpr auto kLanguages = languages(kLanguageEnglishUs);
constexpr auto kManufacturer = stringDescriptor(u"TinyUSB");

} // namespace

TEST_CASE("[usb::descriptor] the device descriptor matches the reference") {
   CHECK(bytesOf(kDevice) == kReferenceDevice);
}

TEST_CASE("[usb::descriptor] the CDC + mass storage configuration matches the reference") {
   CHECK(bytesOf(kConfiguration) == kReferenceConfiguration);
}

namespace {

constexpr ConfigurationFields makeConfigurationFields(uint16_t maxPowerMilliamps) {
   ConfigurationFields fields{};
   fields.selfPowered = true;
   fields.remoteWakeup = true;
   fields.maxPowerMilliamps = maxPowerMilliamps;
   return fields;
}

constexpr auto kOddPower = configuration(makeConfigurationFields(99u));
constexpr auto kMaxPower = configuration(makeConfigurationFields(500u));
// configuration(makeConfigurationFields(501u)) does not compile (maxPowerAbove500Milliamps).

} // namespace

TEST_CASE("[usb::descriptor] a builder with valid values works at run time") {
   // The checks trap only for invalid values; this binary is built at -O0, so nothing optimizes the checks away.
   volatile uint16_t milliamps = 100u;
   ConfigurationFields fields{};
   fields.maxPowerMilliamps = milliamps;
   const auto runtime = configuration(fields, interfaceDescriptor(InterfaceFields{}));
   CHECK(runtime[4] == 1u);
   CHECK(runtime[8] == 50u);
}

TEST_CASE("[usb::descriptor] configuration attributes and power") {
   CHECK(bytesOf(kOddPower) == std::vector<uint8_t>{ 0x09, 0x02, 0x09, 0x00, 0x00, 0x01, 0x00, 0xE0, 50u });
   CHECK(kMaxPower[8] == 0xFAu);
}

TEST_CASE("[usb::descriptor] alternate settings do not count as interfaces") {
   constexpr auto withAlternate =
   configuration(kConfigurationFields,
                 interfaceDescriptor(InterfaceFields{ 0u, 0u, 0xFFu, 0u, 0u, 0u }, endpoint(0x81u, EndpointType::Bulk, 64u)),
                 interfaceDescriptor(InterfaceFields{ 0u, 1u, 0xFFu, 0u, 0u, 0u }),
                 interfaceDescriptor(InterfaceFields{ 1u, 0u, 0xFFu, 0u, 0u, 0u }));
   static_assert(withAlternate[4] == 2u, "two interfaces, one with two alternate settings");
   // bNumEndpoints of each interface.
   CHECK(withAlternate[9 + 4] == 1u);
   CHECK(withAlternate[9 + 9 + 7 + 4] == 0u);
}

TEST_CASE("[usb::descriptor] CDC functional descriptors") {
   CHECK(bytesOf(cdcHeader(0x0110u)) == std::vector<uint8_t>{ 0x05, 0x24, 0x00, 0x10, 0x01 });
   CHECK(bytesOf(cdcAcm(0u)) == std::vector<uint8_t>{ 0x04, 0x24, 0x02, 0x00 });
   CHECK(bytesOf(cdcUnion(0u, 1u, 2u)) == std::vector<uint8_t>{ 0x06, 0x24, 0x06, 0x00, 0x01, 0x02 });
}

namespace {

// A character outside ASCII: U+00B0 DEGREE SIGN, and one outside the BMP as a surrogate pair.
constexpr auto kDegreeCelsius = stringDescriptor(u"\u00B0C");
constexpr auto kFire = stringDescriptor(u"\U0001F525");
constexpr auto kEmpty = stringDescriptor(u"");
// A char16_t array without a terminating null does not compile (stringDescriptorNeedsNullTerminatedLiteral):
// constexpr char16_t kName[3]{ u'a', u'b', u'c' }; stringDescriptor(kName);

} // namespace

TEST_CASE("[usb::descriptor] string descriptors") {
   CHECK(bytesOf(kLanguages) == std::vector<uint8_t>{ 0x04, 0x03, 0x09, 0x04 });
   CHECK(bytesOf(languages(0x0409u, 0x0407u)) == std::vector<uint8_t>{ 0x06, 0x03, 0x09, 0x04, 0x07, 0x04 });
   CHECK(bytesOf(kManufacturer) == std::vector<uint8_t>{ 0x10, 0x03, 'T', 0, 'i', 0, 'n', 0, 'y', 0, 'U', 0, 'S', 0, 'B', 0 });
   CHECK(bytesOf(kDegreeCelsius) == std::vector<uint8_t>{ 0x06, 0x03, 0xB0, 0x00, 'C', 0 });
   CHECK(bytesOf(kFire) == std::vector<uint8_t>{ 0x06, 0x03, 0x3D, 0xD8, 0x25, 0xDD });
   CHECK(bytesOf(kEmpty) == std::vector<uint8_t>{ 0x02, 0x03 });
}

// The ID length is part of the type: an ID of another length, or a span of dynamic extent, does not compile.
template <typename Id, typename = void>
struct AcceptsId : std::false_type {};

template <typename Id>
struct AcceptsId<Id, decltype(std::declval<SerialNumber<3> &>().set(std::declval<Id>()))> : std::true_type {};

static_assert(AcceptsId<const std::array<uint8_t, 3> &>::value, "an array of the ID length");
static_assert(AcceptsId<const uint8_t (&)[3]>::value, "a C array of the ID length");
static_assert(AcceptsId<art::span<const uint8_t, 3>>::value, "a span of the ID length");
static_assert(AcceptsId<art::span<uint8_t, 3>>::value, "a span of mutable bytes of the ID length");
static_assert(!AcceptsId<const std::array<uint8_t, 4> &>::value, "a longer array");
static_assert(!AcceptsId<const std::array<uint8_t, 2> &>::value, "a shorter array");
static_assert(!AcceptsId<const uint8_t (&)[4]>::value, "a longer C array");
static_assert(!AcceptsId<art::span<const uint8_t, 4>>::value, "a longer span");
static_assert(!AcceptsId<art::span<const uint8_t>>::value, "a span of dynamic extent");
static_assert(!AcceptsId<const std::array<uint16_t, 3> &>::value, "an array of other elements");

TEST_CASE("[usb::descriptor] the serial number is filled at run time") {
   SerialNumber<3> serial;
   CHECK(std::vector<uint8_t>(serial.bytes().begin(), serial.bytes().end()) ==
         std::vector<uint8_t>{ 0x0E, 0x03, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0 });

   const std::array<uint8_t, 3> id{ 0x12u, 0xABu, 0x0Fu };
   serial.set(id);
   CHECK(std::vector<uint8_t>(serial.bytes().begin(), serial.bytes().end()) ==
         std::vector<uint8_t>{ 0x0E, 0x03, '1', 0, '2', 0, 'A', 0, 'B', 0, '0', 0, 'F', 0 });

   const std::array<uint8_t, 3> other{ 0xC0u, 0xFFu, 0xEEu };
   serial.set(art::span<const uint8_t, 3>(other.data(), other.size()));
   CHECK(std::vector<uint8_t>(serial.bytes().begin(), serial.bytes().end()) ==
         std::vector<uint8_t>{ 0x0E, 0x03, 'C', 0, '0', 0, 'F', 0, 'F', 0, 'E', 0, 'E', 0 });

   CHECK(SerialNumber<12>::kSize == 50u);
}

TEST_CASE("[usb::descriptor] usb::Device serves and uses the built descriptors") {
   static constexpr auto kProduct = stringDescriptor(u"Oven controller");
   SerialNumber<12> serial;
   const std::array<uint8_t, 12> uid{ 0x01u, 0x23u, 0x45u, 0x67u, 0x89u, 0xABu, 0xCDu, 0xEFu, 0x00u, 0x11u, 0x22u, 0x33u };
   serial.set(uid);
   const std::array<art::span<const uint8_t>, 4> strings{ asSpan(kLanguages), asSpan(kManufacturer), asSpan(kProduct),
                                                          serial.bytes() };
   Descriptors descriptors{};
   descriptors.device = asSpan(kDevice);
   descriptors.configuration = asSpan(kConfiguration);
   descriptors.strings = art::span<const art::span<const uint8_t>>(strings.data(), strings.size());

   test::FakeDeviceController controller;
   Device usbDevice(controller, descriptors);
   usbDevice.start();
   controller.busReset();

   const auto getDescriptor = [&controller](DescriptorType type, uint8_t index) {
      bool ok = false;
      const test::FakeDeviceController::ControlResult result = controller.controlIn(
      test::setupPacket(0x80u, 6u, static_cast<uint16_t>((static_cast<uint16_t>(type) << 8u) | index), 0x0409u, 255u), &ok);
      CHECK(ok);
      CHECK_FALSE(result.stalled);
      return result.data;
   };
   CHECK(getDescriptor(DescriptorType::Device, 0u) == kReferenceDevice);
   CHECK(getDescriptor(DescriptorType::Configuration, 0u) == kReferenceConfiguration);
   const std::vector<uint8_t> serialText = getDescriptor(DescriptorType::String, 3u);
   REQUIRE(serialText.size() == 50u);
   CHECK(serialText[2] == '0');
   CHECK(serialText[3] == 0u);
   CHECK(serialText[4] == '1');
   CHECK(serialText[48] == '3');

   bool ok = false;
   controller.controlOut(test::setupPacket(0x00u, 5u, 4u, 0u, 0u), art::span<const uint8_t>(), &ok);
   CHECK(ok);
   controller.controlOut(test::setupPacket(0x00u, 9u, 1u, 0u, 0u), art::span<const uint8_t>(), &ok);
   CHECK(ok);
   CHECK(usbDevice.state() == DeviceState::Configured);
   for(const uint8_t endpointAddress : { 0x81u, 0x02u, 0x82u, 0x03u, 0x83u }) {
      CHECK(controller.isOpen(endpointAddress));
   }
}
