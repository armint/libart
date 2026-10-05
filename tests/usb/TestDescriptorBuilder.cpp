#include "FakeDeviceController.h"
#include "usb/DescriptorBuilder.h"
#include "usb/Device.h"
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using namespace art::usb;
using namespace art::usb::descriptor;

namespace {

template <size_t Size>
std::vector<uint8_t> bytesOf(const std::array<uint8_t, Size> &descriptor) {
   return std::vector<uint8_t>(descriptor.begin(), descriptor.end());
}

// The reference: the descriptors of the TinyUSB example "cdc_msc" (examples/device/cdc_msc/src/usb_descriptors.c),
// a CDC-ACM + mass storage composite device, as the TUD_CONFIG_DESCRIPTOR, TUD_CDC_DESCRIPTOR and TUD_MSC_DESCRIPTOR
// macros expand them. Interfaces: 0 CDC control (string 4), 1 CDC data, 2 mass storage (string 5). Endpoints: 0x81
// notification (8 bytes), 0x02/0x82 CDC data, 0x03/0x83 mass storage (64 bytes).
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

TEST_CASE("[usb::descriptor] configuration attributes and power") {
   ConfigurationFields fields{};
   fields.selfPowered = true;
   fields.remoteWakeup = true;
   fields.maxPowerMilliamps = 99u;
   const auto selfPowered = configuration(fields);
   CHECK(bytesOf(selfPowered) == std::vector<uint8_t>{ 0x09, 0x02, 0x09, 0x00, 0x00, 0x01, 0x00, 0xE0, 50u });

   fields.maxPowerMilliamps = 900u;
   CHECK(configuration(fields)[8] == 250u);
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

TEST_CASE("[usb::descriptor] string descriptors") {
   CHECK(bytesOf(kLanguages) == std::vector<uint8_t>{ 0x04, 0x03, 0x09, 0x04 });
   CHECK(bytesOf(languages(0x0409u, 0x0407u)) == std::vector<uint8_t>{ 0x06, 0x03, 0x09, 0x04, 0x07, 0x04 });
   CHECK(bytesOf(kManufacturer) == std::vector<uint8_t>{ 0x10, 0x03, 'T', 0, 'i', 0, 'n', 0, 'y', 0, 'U', 0, 'S', 0, 'B', 0 });
   // A character outside ASCII: U+00B0 DEGREE SIGN, and one outside the BMP as a surrogate pair.
   CHECK(bytesOf(stringDescriptor(u"°C")) == std::vector<uint8_t>{ 0x06, 0x03, 0xB0, 0x00, 'C', 0 });
   CHECK(bytesOf(stringDescriptor(u"\U0001F525")) == std::vector<uint8_t>{ 0x06, 0x03, 0x3D, 0xD8, 0x25, 0xDD });
   CHECK(bytesOf(stringDescriptor(u"")) == std::vector<uint8_t>{ 0x02, 0x03 });
}

TEST_CASE("[usb::descriptor] the serial number is filled at run time") {
   SerialNumber<3> serial;
   CHECK(std::vector<uint8_t>(serial.bytes().begin(), serial.bytes().end()) ==
         std::vector<uint8_t>{ 0x0E, 0x03, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0 });

   const std::array<uint8_t, 3> id{ 0x12u, 0xABu, 0x0Fu };
   serial.set(art::span<const uint8_t>(id.data(), id.size()));
   CHECK(std::vector<uint8_t>(serial.bytes().begin(), serial.bytes().end()) ==
         std::vector<uint8_t>{ 0x0E, 0x03, '1', 0, '2', 0, 'A', 0, 'B', 0, '0', 0, 'F', 0 });

   // A shorter ID leaves zeros, a longer one is cut.
   serial.set(art::span<const uint8_t>(id.data(), 1u));
   CHECK(std::vector<uint8_t>(serial.bytes().begin(), serial.bytes().end()) ==
         std::vector<uint8_t>{ 0x0E, 0x03, '1', 0, '2', 0, '0', 0, '0', 0, '0', 0, '0', 0 });
   SerialNumber<1> shortSerial;
   shortSerial.set(art::span<const uint8_t>(id.data(), id.size()));
   CHECK(std::vector<uint8_t>(shortSerial.bytes().begin(), shortSerial.bytes().end()) ==
         std::vector<uint8_t>{ 0x06, 0x03, '1', 0, '2', 0 });

   CHECK(SerialNumber<12>::kSize == 50u);
}

TEST_CASE("[usb::descriptor] usb::Device serves and uses the built descriptors") {
   static constexpr auto kProduct = stringDescriptor(u"Oven controller");
   SerialNumber<12> serial;
   const std::array<uint8_t, 12> uid{ 0x01u, 0x23u, 0x45u, 0x67u, 0x89u, 0xABu, 0xCDu, 0xEFu, 0x00u, 0x11u, 0x22u, 0x33u };
   serial.set(art::span<const uint8_t>(uid.data(), uid.size()));
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
