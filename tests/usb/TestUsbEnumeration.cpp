#include "FakeDeviceController.h"
#include "usb/DescriptorBuilder.h"
#include "usb/Device.h"
#include "usb/Function.h"
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <optional>
#include <vector>

// Enumeration scripts of real hosts, run against usb::Device with the descriptor set that the oven controller will
// use (CDC-ACM + mass storage, built with usb/DescriptorBuilder.h). Each script runs twice: with the events
// delivered at once, and with the events queued until the main loop polls, as the STM32 controller delivers them.

using namespace art::usb;
using namespace art::usb::descriptor;
using art::usb::test::FakeDeviceController;
using art::usb::test::setupPacket;

namespace {

constexpr uint8_t kStandardDeviceIn = 0x80u;
constexpr uint8_t kStandardDeviceOut = 0x00u;
constexpr uint8_t kStandardInterfaceIn = 0x81u;
constexpr uint8_t kStandardInterfaceOut = 0x01u;
constexpr uint8_t kStandardEndpointIn = 0x82u;
constexpr uint8_t kStandardEndpointOut = 0x02u;

constexpr uint8_t kGetStatus = 0u;
constexpr uint8_t kClearFeature = 1u;
constexpr uint8_t kSetFeature = 3u;
constexpr uint8_t kSetAddress = 5u;
constexpr uint8_t kGetDescriptor = 6u;
constexpr uint8_t kSetDescriptor = 7u;
constexpr uint8_t kGetConfiguration = 8u;
constexpr uint8_t kSetConfiguration = 9u;
constexpr uint8_t kGetInterface = 10u;
constexpr uint8_t kSetInterface = 11u;

constexpr uint16_t descriptorValue(DescriptorType type, uint8_t index = 0u) {
   return static_cast<uint16_t>((static_cast<uint16_t>(type) << 8u) | index);
}

constexpr DeviceFields makeDeviceFields() {
   DeviceFields fields{};
   fields.bDeviceClass = classcode::kMiscellaneous;
   fields.bDeviceSubClass = classcode::kMiscellaneousSubclassCommon;
   fields.bDeviceProtocol = classcode::kMiscellaneousProtocolIad;
   fields.idVendor = 0x1234u;
   fields.idProduct = 0x5678u;
   fields.bcdDevice = 0x0100u;
   fields.iManufacturer = 1u;
   fields.iProduct = 2u;
   fields.iSerialNumber = 3u;
   return fields;
}

constexpr ConfigurationFields makeConfigurationFields() {
   ConfigurationFields fields{};
   fields.selfPowered = true;
   return fields;
}

constexpr auto kDevice = device(makeDeviceFields());

constexpr auto kCdc = concat(interfaceAssociation(0u, 2u, classcode::kCdc, cdc::kSubclassAcm, 0u),
                             interfaceDescriptor(InterfaceFields{ 0u, 0u, classcode::kCdc, cdc::kSubclassAcm, 0u, 0u },
                                                 cdcHeader(),
                                                 cdcCallManagement(0u, 1u),
                                                 cdcAcm(),
                                                 cdcUnion(0u, 1u),
                                                 endpoint(0x81u, EndpointType::Interrupt, 16u, 16u)),
                             interfaceDescriptor(InterfaceFields{ 1u, 0u, classcode::kCdcData, 0u, 0u, 0u },
                                                 endpoint(0x02u, EndpointType::Bulk, 64u),
                                                 endpoint(0x82u, EndpointType::Bulk, 64u)));

constexpr auto kMsc = interfaceDescriptor(InterfaceFields{ 2u, 0u, classcode::kMassStorage, 0x06u, 0x50u, 0u },
                                          endpoint(0x03u, EndpointType::Bulk, 64u),
                                          endpoint(0x83u, EndpointType::Bulk, 64u));

/// The configuration of the oven controller: 98 bytes, so a read with wLength 255 ends with a short packet.
constexpr auto kConfiguration = configuration(makeConfigurationFields(), kCdc, kMsc);
static_assert(kConfiguration.size() == 98u, "the composite configuration");

/// The same with a vendor interface of three endpoints: exactly 128 bytes, so a read with wLength 255 needs a ZLP.
constexpr auto kConfiguration128 = configuration(makeConfigurationFields(),
                                                 kCdc,
                                                 kMsc,
                                                 interfaceDescriptor(InterfaceFields{ 3u, 0u, 0xFFu, 0u, 0u, 0u },
                                                                     endpoint(0x04u, EndpointType::Bulk, 64u),
                                                                     endpoint(0x84u, EndpointType::Bulk, 64u),
                                                                     endpoint(0x85u, EndpointType::Interrupt, 8u, 10u)));
static_assert(kConfiguration128.size() == 128u, "two full packets");

constexpr auto kLanguages = languages(kLanguageEnglishUs);
constexpr auto kManufacturer = stringDescriptor(u"Example");
constexpr auto kProduct = stringDescriptor(u"Oven controller");

class CountingFunction : public Function {
   public:
   using Function::Function;

   void configured() override {
      ++configuredCount;
   }

   int configuredCount{ 0 };
};

/// A started device on the fake controller, with the CDC and mass storage functions and a 12-byte serial number.
class Bench {
   public:
   Bench(art::span<const uint8_t> configurationSet, bool queueEvents)
      : strings_{ asSpan(kLanguages), asSpan(kManufacturer), asSpan(kProduct), serial_.bytes() }
      , descriptors_{ asSpan(kDevice), configurationSet,
                      art::span<const art::span<const uint8_t>>(strings_.data(), strings_.size()) }
      , device_(controller_, descriptors_) {
      const std::array<uint8_t, 12> uid{ 0x30u, 0x31u, 0x32u, 0x33u, 0x34u, 0x35u,
                                         0x36u, 0x37u, 0x38u, 0x39u, 0x3Au, 0x3Bu };
      serial_.set(uid);
      REQUIRE(device_.addFunction(cdc));
      REQUIRE(device_.addFunction(msc));
      controller_.queueEvents(queueEvents);
      device_.start();
   }

   FakeDeviceController &controller() {
      return controller_;
   }

   Device &device() {
      return device_;
   }

   /// A device-to-host request that must succeed.
   FakeDeviceController::ControlResult in(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
      bool ok = false;
      FakeDeviceController::ControlResult result =
      controller_.controlIn(setupPacket(requestType, request, value, index, length), &ok);
      CHECK(ok);
      return result;
   }

   std::vector<uint8_t> getDescriptor(DescriptorType type, uint8_t index, uint16_t languageId, uint16_t length) {
      const FakeDeviceController::ControlResult result =
      in(kStandardDeviceIn, kGetDescriptor, descriptorValue(type, index), languageId, length);
      CHECK_FALSE(result.stalled);
      return result.data;
   }

   /// A host-to-device request without data stage. Returns false if the device stalled it.
   bool out(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index) {
      bool ok = false;
      const FakeDeviceController::ControlResult result =
      controller_.controlOut(setupPacket(requestType, request, value, index, 0u), art::span<const uint8_t>(), &ok);
      CHECK(ok);
      return !result.stalled;
   }

   /// The host reads one IN packet; it waits for the main loop if the endpoint NAKs.
   std::optional<std::vector<uint8_t>> takeIn(uint8_t endpointAddress) {
      std::optional<std::vector<uint8_t>> packet = controller_.takeIn(endpointAddress);
      if(!packet.has_value()) {
         controller_.deliverQueuedEvents();
         packet = controller_.takeIn(endpointAddress);
      }
      return packet;
   }

   /// Checks the state after SET_CONFIGURATION 1.
   void checkConfigured(uint8_t address) {
      controller_.deliverQueuedEvents();
      CHECK(device_.state() == DeviceState::Configured);
      CHECK(device_.address() == address);
      CHECK(controller_.address() == address);
      CHECK(device_.configuration() == 1u);
      for(const uint8_t endpointAddress : { 0x81u, 0x02u, 0x82u, 0x03u, 0x83u }) {
         CAPTURE(endpointAddress);
         CHECK(controller_.isOpen(endpointAddress));
      }
      CHECK(cdc.configuredCount == 1);
      CHECK(msc.configuredCount == 1);
   }

   CountingFunction cdc{ 0u, 2u };
   CountingFunction msc{ 2u, 1u };

   private:
   SerialNumber<12> serial_;
   std::array<art::span<const uint8_t>, 4> strings_;
   Descriptors descriptors_;
   FakeDeviceController controller_;
   Device device_;
};

std::vector<uint8_t> bytesOf(art::span<const uint8_t> data) {
   return std::vector<uint8_t>(data.begin(), data.end());
}

/// The serial number of the bench: the UID bytes 0x30..0x3B as hexadecimal digits.
std::vector<uint8_t> expectedSerial() {
   std::vector<uint8_t> serial{ 50u, 0x03u };
   for(uint8_t value = 0x30u; value <= 0x3Bu; ++value) {
      const char digits[] = "0123456789ABCDEF";
      serial.push_back(static_cast<uint8_t>(digits[value >> 4u]));
      serial.push_back(0u);
      serial.push_back(static_cast<uint8_t>(digits[value & 0x0Fu]));
      serial.push_back(0u);
   }
   return serial;
}

/// The packet sizes of a data stage of `length` bytes for wLength `requested`, with packets of 64 bytes.
std::vector<size_t> expectedPackets(size_t length, size_t requested) {
   const size_t sent = std::min(length, requested);
   std::vector<size_t> packets(sent / 64u, 64u);
   if(sent % 64u != 0u) {
      packets.push_back(sent % 64u);
   } else if(sent < requested) {
      packets.push_back(0u);
   }
   return packets;
}

/**
 * The enumeration of the Linux USB core (hub_port_init(), usb_new_device()): a 64-byte read of the device descriptor
 * at address 0, a second reset, SET_ADDRESS, the device descriptor, the configuration in two reads (header, then
 * wTotalLength), the strings, SET_CONFIGURATION.
 */
void enumerateLikeLinux(Bench &bench, art::span<const uint8_t> configurationSet) {
   FakeDeviceController &controller = bench.controller();
   controller.busReset();
   const FakeDeviceController::ControlResult first =
   bench.in(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::Device), 0u, 64u);
   CHECK(first.data == bytesOf(asSpan(kDevice)));
   CHECK(first.packetSizes == std::vector<size_t>{ 18u });

   controller.busReset();
   CHECK(bench.out(kStandardDeviceOut, kSetAddress, 21u, 0u));
   CHECK(controller.address() == 21u);
   CHECK(bench.getDescriptor(DescriptorType::Device, 0u, 0u, 18u) == bytesOf(asSpan(kDevice)));

   const std::vector<uint8_t> header = bench.getDescriptor(DescriptorType::Configuration, 0u, 0u, 9u);
   REQUIRE(header.size() == 9u);
   const auto totalLength = static_cast<uint16_t>(header[2] | (header[3] << 8u));
   CHECK(totalLength == configurationSet.size());
   const FakeDeviceController::ControlResult full =
   bench.in(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::Configuration), 0u, totalLength);
   CHECK(full.data == bytesOf(configurationSet));
   CHECK(full.packetSizes == expectedPackets(configurationSet.size(), totalLength));

   CHECK(bench.getDescriptor(DescriptorType::String, 0u, 0u, 255u) == bytesOf(asSpan(kLanguages)));
   CHECK(bench.getDescriptor(DescriptorType::String, 2u, kLanguageEnglishUs, 255u) == bytesOf(asSpan(kProduct)));
   CHECK(bench.getDescriptor(DescriptorType::String, 1u, kLanguageEnglishUs, 255u) == bytesOf(asSpan(kManufacturer)));
   CHECK(bench.getDescriptor(DescriptorType::String, 3u, kLanguageEnglishUs, 255u) == expectedSerial());

   CHECK(bench.out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
   bench.checkConfigured(21u);
}

/**
 * The enumeration of Windows 10/11: a 64-byte read of the device descriptor at address 0 that the host ends with a
 * bus reset after the first data packet (no status stage), SET_ADDRESS, the device descriptor, the configuration with
 * wLength 255, string 0 and the serial number, the device qualifier (which a full-speed device stalls), the
 * configuration in two reads, GET_STATUS, the product string, SET_CONFIGURATION.
 */
void enumerateLikeWindows(Bench &bench, art::span<const uint8_t> configurationSet) {
   FakeDeviceController &controller = bench.controller();
   controller.busReset();
   controller.sendSetup(setupPacket(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::Device), 0u, 64u));
   const std::optional<std::vector<uint8_t>> first = bench.takeIn(0x80u);
   REQUIRE(first.has_value());
   CHECK(*first == bytesOf(asSpan(kDevice)));
   controller.busReset();

   CHECK(bench.out(kStandardDeviceOut, kSetAddress, 5u, 0u));
   CHECK(bench.getDescriptor(DescriptorType::Device, 0u, 0u, 18u) == bytesOf(asSpan(kDevice)));
   const FakeDeviceController::ControlResult all =
   bench.in(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::Configuration), 0u, 255u);
   CHECK(all.data == bytesOf(configurationSet));
   CHECK(all.packetSizes == expectedPackets(configurationSet.size(), 255u));

   CHECK(bench.getDescriptor(DescriptorType::String, 0u, 0u, 255u) == bytesOf(asSpan(kLanguages)));
   CHECK(bench.getDescriptor(DescriptorType::String, 3u, kLanguageEnglishUs, 255u) == expectedSerial());
   CHECK(bench.in(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::DeviceQualifier), 0u, 10u).stalled);

   CHECK(bench.getDescriptor(DescriptorType::Configuration, 0u, 0u, 9u).size() == 9u);
   CHECK(bench.getDescriptor(DescriptorType::Configuration, 0u, 0u, static_cast<uint16_t>(configurationSet.size())) ==
         bytesOf(configurationSet));
   CHECK(bench.in(kStandardDeviceIn, kGetStatus, 0u, 0u, 2u).data == std::vector<uint8_t>{ 0x01u, 0x00u });
   CHECK(bench.getDescriptor(DescriptorType::String, 2u, kLanguageEnglishUs, 255u) == bytesOf(asSpan(kProduct)));

   CHECK(bench.out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
   bench.checkConfigured(5u);
}

} // namespace

TEST_CASE("[usb::Device] enumeration by Linux") {
   for(const bool queued : { false, true }) {
      CAPTURE(queued);
      {
         Bench bench(asSpan(kConfiguration), queued);
         enumerateLikeLinux(bench, asSpan(kConfiguration));
      }
      {
         // Exactly two packets: the read with wLength = wTotalLength ends without a ZLP.
         Bench bench(asSpan(kConfiguration128), queued);
         enumerateLikeLinux(bench, asSpan(kConfiguration128));
      }
   }
}

TEST_CASE("[usb::Device] enumeration by Windows") {
   for(const bool queued : { false, true }) {
      CAPTURE(queued);
      {
         Bench bench(asSpan(kConfiguration), queued);
         enumerateLikeWindows(bench, asSpan(kConfiguration));
      }
      {
         // Exactly two packets: the read with wLength 255 ends with a ZLP.
         Bench bench(asSpan(kConfiguration128), queued);
         enumerateLikeWindows(bench, asSpan(kConfiguration128));
      }
   }
}

TEST_CASE("[usb::Device] a configured device enumerates again after a bus reset") {
   for(const bool queued : { false, true }) {
      CAPTURE(queued);
      Bench bench(asSpan(kConfiguration), queued);
      enumerateLikeLinux(bench, asSpan(kConfiguration));
      // The host suspends and resets the port, for example after a resume from sleep.
      bench.controller().busSuspend();
      bench.controller().busReset();
      bench.controller().deliverQueuedEvents();
      CHECK(bench.device().state() == DeviceState::Default);
      CHECK_FALSE(bench.controller().isOpen(0x83u));
      bench.cdc.configuredCount = 0;
      bench.msc.configuredCount = 0;
      enumerateLikeWindows(bench, asSpan(kConfiguration));
   }
}

TEST_CASE("[usb::Device] SET_ADDRESS in the Address state changes the address") {
   for(const bool queued : { false, true }) {
      CAPTURE(queued);
      Bench bench(asSpan(kConfiguration), queued);
      bench.controller().busReset();
      CHECK(bench.out(kStandardDeviceOut, kSetAddress, 3u, 0u));
      CHECK(bench.out(kStandardDeviceOut, kSetAddress, 4u, 0u));
      bench.controller().deliverQueuedEvents();
      CHECK(bench.device().address() == 4u);
      CHECK(bench.controller().address() == 4u);
      CHECK(bench.device().state() == DeviceState::Address);
   }
}

TEST_CASE("[usb::Device] malformed standard requests stall") {
   for(const bool queued : { false, true }) {
      CAPTURE(queued);
      Bench bench(asSpan(kConfiguration), queued);
      enumerateLikeLinux(bench, asSpan(kConfiguration));

      struct Case {
         const char *name;
         uint8_t requestType;
         uint8_t request;
         uint16_t value;
         uint16_t index;
         uint16_t length;
      };
      const std::array<Case, 14> cases{ {
      { "reserved request code 2", kStandardDeviceIn, 2u, 0u, 0u, 2u },
      { "reserved request code 4", kStandardDeviceOut, 4u, 0u, 0u, 0u },
      { "recipient Other", 0x83u, kGetStatus, 0u, 0u, 2u },
      { "reserved recipient", 0x84u, kGetStatus, 0u, 0u, 2u },
      { "request type Reserved", 0xE0u, kGetStatus, 0u, 0u, 2u },
      { "GET_STATUS of the device with wValue 1", kStandardDeviceIn, kGetStatus, 1u, 0u, 2u },
      { "GET_CONFIGURATION with wIndex 1", kStandardDeviceIn, kGetConfiguration, 0u, 1u, 1u },
      { "SET_CONFIGURATION with a high byte in wValue", kStandardDeviceOut, kSetConfiguration, 0x0101u, 0u, 0u },
      { "SET_ADDRESS with wIndex 1", kStandardDeviceOut, kSetAddress, 7u, 1u, 0u },
      { "CLEAR_FEATURE of an endpoint with selector 1", kStandardEndpointOut, kClearFeature, 1u, 0x81u, 0u },
      { "SET_FEATURE of an endpoint with a high byte in wIndex", kStandardEndpointOut, kSetFeature, 0u, 0x0181u, 0u },
      { "GET_STATUS of an endpoint with a reserved address bit", kStandardEndpointIn, kGetStatus, 0u, 0x91u, 2u },
      { "GET_INTERFACE with a high byte in wIndex", kStandardInterfaceIn, kGetInterface, 0u, 0x0100u, 1u },
      { "SET_INTERFACE with wValue above 255", kStandardInterfaceOut, kSetInterface, 0x0100u, 0u, 0u },
      } };
      for(const Case &testCase : cases) {
         CAPTURE(testCase.name);
         bool ok = false;
         const SetupPacket setup =
         setupPacket(testCase.requestType, testCase.request, testCase.value, testCase.index, testCase.length);
         const FakeDeviceController::ControlResult result = (testCase.requestType & 0x80u) != 0u ?
                                                            bench.controller().controlIn(setup, &ok) :
                                                            bench.controller().controlOut(setup, {}, &ok);
         CHECK(ok);
         CHECK(result.stalled);
      }

      // Requests with a data stage where none is allowed stall after the data stage.
      const std::array<uint8_t, 1> byte{ 0u };
      bool ok = false;
      CHECK(bench.controller()
            .controlOut(setupPacket(kStandardInterfaceOut, kSetInterface, 0u, 2u, 1u),
                        art::span<const uint8_t>(byte.data(), byte.size()), &ok)
            .stalled);
      CHECK(ok);
      const std::array<uint8_t, 18> descriptor{};
      CHECK(bench.controller()
            .controlOut(setupPacket(kStandardDeviceOut, kSetDescriptor, descriptorValue(DescriptorType::Device), 0u, 18u),
                        art::span<const uint8_t>(descriptor.data(), descriptor.size()), &ok)
            .stalled);
      CHECK(ok);

      // None of them changed the device.
      bench.controller().deliverQueuedEvents();
      CHECK(bench.device().state() == DeviceState::Configured);
      CHECK(bench.device().address() == 21u);
      CHECK(bench.device().alternateSetting(2u) == 0u);
      CHECK_FALSE(bench.device().isEndpointHalted(0x81u));
      CHECK(bench.in(kStandardDeviceIn, kGetConfiguration, 0u, 0u, 1u).data == std::vector<uint8_t>{ 1u });
   }
}
