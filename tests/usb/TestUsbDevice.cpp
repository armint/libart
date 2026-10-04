#include "FakeDeviceController.h"
#include "usb/Device.h"
#include "usb/Function.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <optional>
#include <utility>
#include <vector>

using namespace art::usb;
using namespace art::usb::test;

namespace {

// bmRequestType values.
constexpr uint8_t kStandardDeviceIn = 0x80u;
constexpr uint8_t kStandardDeviceOut = 0x00u;
constexpr uint8_t kStandardInterfaceIn = 0x81u;
constexpr uint8_t kStandardInterfaceOut = 0x01u;
constexpr uint8_t kStandardEndpointIn = 0x82u;
constexpr uint8_t kStandardEndpointOut = 0x02u;
constexpr uint8_t kClassInterfaceIn = 0xA1u;
constexpr uint8_t kClassInterfaceOut = 0x21u;
constexpr uint8_t kClassEndpointIn = 0xA2u;
constexpr uint8_t kVendorDeviceIn = 0xC0u;

// bRequest values.
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
constexpr uint8_t kSynchFrame = 12u;

constexpr uint16_t descriptorValue(DescriptorType type, uint8_t index = 0u) {
   return static_cast<uint16_t>((static_cast<uint16_t>(type) << 8u) | index);
}

// clang-format off
// A composite device like the oven controller: CDC-ACM (interfaces 0 and 1, grouped by an IAD) and mass storage
// (interface 2, with an alternate setting 1 without endpoints for the SET_INTERFACE tests). EP0 packet size 64.
constexpr std::array<uint8_t, 18> kDeviceDescriptor{
   0x12, 0x01, 0x00, 0x02, 0xEF, 0x02, 0x01, 0x40, 0x03, 0x04, 0x3B, 0x9B, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01
};

constexpr std::array<uint8_t, 107> kConfigurationDescriptor{
   // Configuration: wTotalLength 107, 3 interfaces, value 1, self-powered, 100 mA.
   0x09, 0x02, 107, 0x00, 0x03, 0x01, 0x00, 0xC0, 0x32,
   // IAD: interfaces 0 and 1, CDC.
   0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,
   // Interface 0: CDC communication, 1 endpoint.
   0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
   0x05, 0x24, 0x00, 0x10, 0x01,  // CDC header
   0x05, 0x24, 0x01, 0x00, 0x01,  // CDC call management
   0x04, 0x24, 0x02, 0x02,        // CDC ACM
   0x05, 0x24, 0x06, 0x00, 0x01,  // CDC union
   0x07, 0x05, 0x81, 0x03, 0x10, 0x00, 0x10,  // EP 0x81 interrupt, 16 bytes
   // Interface 1: CDC data, 2 endpoints.
   0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
   0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,  // EP 0x02 bulk, 64 bytes
   0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,  // EP 0x82 bulk, 64 bytes
   // Interface 2, alternate setting 0: mass storage, 2 endpoints.
   0x09, 0x04, 0x02, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
   0x07, 0x05, 0x03, 0x02, 0x40, 0x00, 0x00,  // EP 0x03 bulk, 64 bytes
   0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,  // EP 0x83 bulk, 64 bytes
   // Interface 2, alternate setting 1: no endpoints.
   0x09, 0x04, 0x02, 0x01, 0x00, 0x08, 0x06, 0x50, 0x00,
};
// clang-format on

constexpr std::array<uint8_t, 4> kLanguageIds{ 0x04, 0x03, 0x09, 0x04 };

/// A string descriptor of `Size` bytes (bLength = Size), filled with 'a'.
template <size_t Size>
constexpr std::array<uint8_t, Size> makeString() {
   std::array<uint8_t, Size> descriptor{};
   descriptor[0] = static_cast<uint8_t>(Size);
   descriptor[1] = static_cast<uint8_t>(DescriptorType::String);
   for(size_t index = 2u; index + 1u < Size; index += 2u) {
      descriptor[index] = 'a';
   }
   return descriptor;
}

constexpr auto kString64 = makeString<64>();
constexpr auto kString128 = makeString<128>();
constexpr auto kString100 = makeString<100>();
constexpr auto kString10 = makeString<10>();

const std::array<art::span<const uint8_t>, 5> kStrings{
   art::span<const uint8_t>(kLanguageIds.data(), kLanguageIds.size()),
   art::span<const uint8_t>(kString10.data(), kString10.size()),
   art::span<const uint8_t>(kString64.data(), kString64.size()),
   art::span<const uint8_t>(kString128.data(), kString128.size()),
   art::span<const uint8_t>(kString100.data(), kString100.size()),
};

Descriptors makeDescriptors(art::span<const uint8_t> device = art::span<const uint8_t>(kDeviceDescriptor.data(),
                                                                                       kDeviceDescriptor.size())) {
   Descriptors descriptors{};
   descriptors.device = device;
   descriptors.configuration = art::span<const uint8_t>(kConfigurationDescriptor.data(), kConfigurationDescriptor.size());
   descriptors.strings = art::span<const art::span<const uint8_t>>(kStrings.data(), kStrings.size());
   return descriptors;
}

/// Records every callback; answers class requests with `reply`, or stalls if `accept` is false.
class RecordingFunction : public Function {
   public:
   using Function::Function;

   std::optional<uint16_t> controlRequest(const SetupPacket &setup, art::span<uint8_t> data) override {
      requests.push_back(setup);
      if(setup.direction() == RequestDirection::HostToDevice) {
         received.assign(data.begin(), data.end());
      }
      if(!accept) {
         return std::nullopt;
      }
      const size_t length = std::min(reply.size(), data.size());
      std::copy_n(reply.begin(), length, data.begin());
      // Report the full reply length; the device must clip it to the buffer.
      return static_cast<uint16_t>(reply.size());
   }

   void configured() override {
      ++configuredCount;
   }

   void deconfigured() override {
      ++deconfiguredCount;
   }

   void alternateSettingChanged(uint8_t interfaceNumber, uint8_t alternateSetting) override {
      alternateSettingChanges.emplace_back(interfaceNumber, alternateSetting);
   }

   void suspended() override {
      ++suspendedCount;
   }

   void resumed() override {
      ++resumedCount;
   }

   void outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) override {
      outPackets.emplace_back(endpointAddress, std::vector<uint8_t>(packet.begin(), packet.end()));
   }

   void inPacketSent(uint8_t endpointAddress) override {
      inCompletions.push_back(endpointAddress);
   }

   void endpointHaltCleared(uint8_t endpointAddress) override {
      haltCleared.push_back(endpointAddress);
      if(keepHalted) {
         device()->haltEndpoint(endpointAddress);
      }
   }

   bool accept{ true };
   bool keepHalted{ false };
   std::vector<uint8_t> reply;
   std::vector<uint8_t> received;
   std::vector<SetupPacket> requests;
   int configuredCount{ 0 };
   int deconfiguredCount{ 0 };
   int suspendedCount{ 0 };
   int resumedCount{ 0 };
   std::vector<std::pair<uint8_t, uint8_t>> alternateSettingChanges;
   std::vector<std::pair<uint8_t, std::vector<uint8_t>>> outPackets;
   std::vector<uint8_t> inCompletions;
   std::vector<uint8_t> haltCleared;
};

/// A device with a CDC function (interfaces 0, 1) and a mass storage function (interface 2), started.
struct Fixture {
   explicit Fixture(Descriptors descriptors = makeDescriptors())
      : device(controller, descriptors) {
      REQUIRE(device.addFunction(cdc));
      REQUIRE(device.addFunction(msc));
      device.start();
   }

   FakeDeviceController::ControlResult in(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
      bool ok = false;
      FakeDeviceController::ControlResult result =
      controller.controlIn(setupPacket(requestType, request, value, index, length), &ok);
      CHECK(ok);
      return result;
   }

   bool out(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index) {
      bool ok = false;
      const FakeDeviceController::ControlResult result =
      controller.controlOut(setupPacket(requestType, request, value, index, 0u), art::span<const uint8_t>(), &ok);
      CHECK(ok);
      return !result.stalled;
   }

   std::vector<uint8_t> getDescriptor(DescriptorType type, uint8_t index, uint16_t length) {
      const FakeDeviceController::ControlResult result =
      in(kStandardDeviceIn, kGetDescriptor, descriptorValue(type, index), 0u, length);
      CHECK_FALSE(result.stalled);
      return result.data;
   }

   void enumerate() {
      controller.busReset();
      REQUIRE(out(kStandardDeviceOut, kSetAddress, 7u, 0u));
      REQUIRE(out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
      REQUIRE(device.state() == DeviceState::Configured);
   }

   FakeDeviceController controller;
   Device device;
   RecordingFunction cdc{ 0u, 2u };
   RecordingFunction msc{ 2u, 1u };
};

std::vector<uint8_t> bytesOf(art::span<const uint8_t> data) {
   return std::vector<uint8_t>(data.begin(), data.end());
}

} // namespace

TEST_CASE("[usb::Device] SetupPacket decodes the little-endian wire format") {
   constexpr std::array<uint8_t, 8> wire{ 0xA1, 0x21, 0x34, 0x12, 0x02, 0x00, 0x07, 0x00 };
   constexpr SetupPacket setup = SetupPacket::fromBytes(art::span<const uint8_t>(wire.data(), wire.size()));
   CHECK(setup.bmRequestType == 0xA1u);
   CHECK(setup.bRequest == 0x21u);
   CHECK(setup.wValue == 0x1234u);
   CHECK(setup.wIndex == 2u);
   CHECK(setup.wLength == 7u);
   CHECK(setup.direction() == RequestDirection::DeviceToHost);
   CHECK(setup.type() == RequestType::Class);
   CHECK(setup.recipient() == RequestRecipient::Interface);
}

TEST_CASE("[usb::Device] start and stop connect and disconnect") {
   FakeDeviceController controller;
   Device device(controller, makeDescriptors());
   CHECK(device.state() == DeviceState::Detached);
   CHECK_FALSE(controller.isConnected());

   device.start();
   CHECK(device.state() == DeviceState::Powered);
   CHECK(controller.isConnected());

   controller.busReset();
   CHECK(device.state() == DeviceState::Default);
   REQUIRE(controller.endpoint(0x00u).has_value());
   CHECK(controller.endpoint(0x00u)->type == EndpointType::Control);
   CHECK(controller.endpoint(0x00u)->maxPacketSize == 64u);

   device.stop();
   CHECK(device.state() == DeviceState::Detached);
   CHECK_FALSE(controller.isConnected());
   CHECK(controller.openEndpointCount() == 0u);
}

TEST_CASE("[usb::Device] enumeration script of a host") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   Device &device = fixture.device;

   // Linux and Windows read the first 64 bytes of the device descriptor at address 0, then reset again.
   controller.busReset();
   CHECK(fixture.getDescriptor(DescriptorType::Device, 0u, 64u) ==
         bytesOf(art::span<const uint8_t>(kDeviceDescriptor.data(), kDeviceDescriptor.size())));
   controller.busReset();
   CHECK(device.state() == DeviceState::Default);

   CHECK(fixture.out(kStandardDeviceOut, kSetAddress, 42u, 0u));
   CHECK(device.state() == DeviceState::Address);
   CHECK(device.address() == 42u);
   CHECK(controller.address() == 42u);

   CHECK(fixture.getDescriptor(DescriptorType::Device, 0u, 18u).size() == 18u);
   // The configuration descriptor alone, then the whole set.
   const std::vector<uint8_t> header = fixture.getDescriptor(DescriptorType::Configuration, 0u, 9u);
   REQUIRE(header.size() == 9u);
   const uint16_t totalLength = static_cast<uint16_t>(header[2] | (header[3] << 8u));
   CHECK(totalLength == kConfigurationDescriptor.size());
   CHECK(fixture.getDescriptor(DescriptorType::Configuration, 0u, totalLength) ==
         bytesOf(art::span<const uint8_t>(kConfigurationDescriptor.data(), kConfigurationDescriptor.size())));
   // Windows asks with wLength 255 for the configuration, and for strings.
   CHECK(fixture.getDescriptor(DescriptorType::Configuration, 0u, 255u).size() == kConfigurationDescriptor.size());
   CHECK(fixture.getDescriptor(DescriptorType::String, 0u, 255u) ==
         bytesOf(art::span<const uint8_t>(kLanguageIds.data(), kLanguageIds.size())));
   CHECK(fixture.getDescriptor(DescriptorType::String, 1u, 255u).size() == kString10.size());

   // GET_STATUS of the device: self-powered, no remote wakeup.
   const FakeDeviceController::ControlResult status = fixture.in(kStandardDeviceIn, kGetStatus, 0u, 0u, 2u);
   CHECK(status.data == std::vector<uint8_t>{ 0x01u, 0x00u });

   CHECK(fixture.in(kStandardDeviceIn, kGetConfiguration, 0u, 0u, 1u).data == std::vector<uint8_t>{ 0u });
   CHECK(fixture.out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
   CHECK(device.state() == DeviceState::Configured);
   CHECK(device.configuration() == 1u);
   CHECK(fixture.in(kStandardDeviceIn, kGetConfiguration, 0u, 0u, 1u).data == std::vector<uint8_t>{ 1u });

   // The endpoints of alternate setting 0 of every interface are open, with the descriptor values.
   REQUIRE(controller.endpoint(0x81u).has_value());
   CHECK(controller.endpoint(0x81u)->type == EndpointType::Interrupt);
   CHECK(controller.endpoint(0x81u)->maxPacketSize == 16u);
   for(const uint8_t endpointAddress : { 0x02u, 0x82u, 0x03u, 0x83u }) {
      REQUIRE(controller.endpoint(endpointAddress).has_value());
      CHECK(controller.endpoint(endpointAddress)->type == EndpointType::Bulk);
      CHECK(controller.endpoint(endpointAddress)->maxPacketSize == 64u);
   }
   // EP0 (both directions) and five endpoints.
   CHECK(controller.openEndpointCount() == 7u);
   CHECK(fixture.cdc.configuredCount == 1);
   CHECK(fixture.msc.configuredCount == 1);

   // SET_CONFIGURATION 0 returns to Address and closes the endpoints.
   CHECK(fixture.out(kStandardDeviceOut, kSetConfiguration, 0u, 0u));
   CHECK(device.state() == DeviceState::Address);
   CHECK(controller.openEndpointCount() == 2u);
   CHECK(fixture.cdc.deconfiguredCount == 1);
}

TEST_CASE("[usb::Device] SET_ADDRESS takes effect after the status stage") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   controller.busReset();

   controller.sendSetup(setupPacket(kStandardDeviceOut, kSetAddress, 9u, 0u, 0u));
   // The controller knows the address before the status stage, but must not use it yet: the status stage is queued,
   // the host has not acknowledged it, and the device still answers at address 0.
   CHECK(controller.hasInPacket(0x80u));
   CHECK(controller.pendingAddress() == std::optional<uint8_t>(9u));
   CHECK(controller.address() == 0u);
   CHECK(fixture.device.state() == DeviceState::Default);
   CHECK(fixture.device.address() == 0u);

   const std::optional<std::vector<uint8_t>> status = controller.takeIn(0x80u);
   REQUIRE(status.has_value());
   CHECK(status->empty());
   CHECK(controller.setAddressCalls() == 1);
   CHECK(controller.address() == 9u);
   CHECK(fixture.device.address() == 9u);
   CHECK(fixture.device.state() == DeviceState::Address);

   SUBCASE("a SETUP before the status stage completes cancels the address") {
      controller.sendSetup(setupPacket(kStandardDeviceOut, kSetAddress, 11u, 0u, 0u));
      // The host aborts and starts a new transfer instead of acknowledging the status stage.
      CHECK(fixture.getDescriptor(DescriptorType::Device, 0u, 18u).size() == 18u);
      CHECK_FALSE(controller.pendingAddress().has_value());
      CHECK(controller.address() == 9u);
      CHECK(fixture.device.address() == 9u);
   }

   SUBCASE("a bus reset before the status stage completes cancels the address") {
      controller.sendSetup(setupPacket(kStandardDeviceOut, kSetAddress, 11u, 0u, 0u));
      controller.busReset();
      CHECK_FALSE(controller.pendingAddress().has_value());
      CHECK(controller.address() == 0u);
      CHECK(fixture.device.address() == 0u);
      CHECK(fixture.device.state() == DeviceState::Default);
   }

   SUBCASE("SET_ADDRESS 0 returns to the Default state") {
      CHECK(fixture.out(kStandardDeviceOut, kSetAddress, 0u, 0u));
      CHECK(fixture.device.state() == DeviceState::Default);
      CHECK(controller.address() == 0u);
   }
}

TEST_CASE("[usb::Device] SET_ADDRESS works when the main loop sees the status stage late") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   controller.busReset();
   controller.queueInEvents(true);

   controller.sendSetup(setupPacket(kStandardDeviceOut, kSetAddress, 9u, 0u, 0u));
   REQUIRE(controller.takeIn(0x80u).has_value());
   // The interrupt has applied the address; the main loop has not seen the acknowledgement yet. The host can
   // already reach the device at the new address.
   CHECK(controller.address() == 9u);
   CHECK(fixture.device.state() == DeviceState::Default);

   controller.deliverQueuedEvents();
   CHECK(fixture.device.address() == 9u);
   CHECK(fixture.device.state() == DeviceState::Address);
}

TEST_CASE("[usb::Device] EP0 sends a zero-length packet only after a full packet shorter than wLength") {
   Fixture fixture;
   fixture.controller.busReset();

   struct Case {
      uint8_t stringIndex;
      uint16_t wLength;
      std::vector<size_t> packets;
   };
   const std::array<Case, 8> cases{ {
   // 64 bytes, exact multiple of the packet size: ZLP only if the host asked for more.
   { 2u, 255u, { 64u, 0u } },
   { 2u, 64u, { 64u } },
   // 128 bytes: two full packets, then a ZLP.
   { 3u, 255u, { 64u, 64u, 0u } },
   { 3u, 128u, { 64u, 64u } },
   // wLength cuts the reply at a packet boundary: no ZLP, the host has all it asked for.
   { 3u, 64u, { 64u } },
   // 100 bytes: the short last packet ends the transfer.
   { 4u, 255u, { 64u, 36u } },
   // wLength cuts the reply inside a packet.
   { 4u, 70u, { 64u, 6u } },
   { 1u, 255u, { 10u } },
   } };
   for(const Case &testCase : cases) {
      CAPTURE(testCase.stringIndex);
      CAPTURE(testCase.wLength);
      const FakeDeviceController::ControlResult result =
      fixture.in(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::String, testCase.stringIndex),
                 0x0409u, testCase.wLength);
      CHECK_FALSE(result.stalled);
      CHECK(result.packetSizes == testCase.packets);
   }
}

TEST_CASE("[usb::Device] the host can end a data-in stage early") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   controller.busReset();

   // 128 bytes for wLength 255: 64, 64 and a ZLP. The host takes the first packet and sends the status stage.
   controller.sendSetup(setupPacket(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::String, 3u), 0x0409u, 255u));
   const std::optional<std::vector<uint8_t>> first = controller.takeIn(0x80u);
   REQUIRE(first.has_value());
   CHECK(first->size() == 64u);
   CHECK(controller.sendOut(0x00u, art::span<const uint8_t>()));

   // The device stops the data stage, and the next request works.
   CHECK(fixture.getDescriptor(DescriptorType::Device, 0u, 18u).size() == 18u);
}

TEST_CASE("[usb::Device] the status stage of a data-in transfer does not wait for the main loop") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   controller.busReset();
   controller.queueInEvents(true);

   controller.sendSetup(setupPacket(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::Device), 0u, 18u));
   const std::optional<std::vector<uint8_t>> data = controller.takeIn(0x80u);
   REQUIRE(data.has_value());
   CHECK(data->size() == 18u);
   // The acknowledgement of the data packet is still queued, but EP0 OUT already accepts the status stage.
   CHECK(controller.sendOut(0x00u, art::span<const uint8_t>()));
   controller.deliverQueuedEvents();

   controller.queueInEvents(false);
   CHECK(fixture.getDescriptor(DescriptorType::Device, 0u, 18u).size() == 18u);
}

TEST_CASE("[usb::Device] an empty function reply is a zero-length data packet") {
   Fixture fixture;
   fixture.enumerate();
   fixture.cdc.reply = {};
   const FakeDeviceController::ControlResult result = fixture.in(kClassInterfaceIn, 0x21u, 0u, 0u, 7u);
   CHECK_FALSE(result.stalled);
   CHECK(result.packetSizes == std::vector<size_t>{ 0u });
}

TEST_CASE("[usb::Device] unsupported and invalid requests stall EP0") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   controller.busReset();

   auto stallsIn = [&](uint8_t requestType, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
      return fixture.in(requestType, request, value, index, length).stalled;
   };
   auto stallsOut = [&](uint8_t requestType, uint8_t request, uint16_t value, uint16_t index) {
      return !fixture.out(requestType, request, value, index);
   };

   SUBCASE("in the Default state") {
      CHECK(stallsOut(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
      CHECK(stallsIn(kStandardDeviceIn, kGetConfiguration, 0u, 0u, 1u));
      CHECK(stallsIn(kStandardDeviceIn, kGetStatus, 0u, 0u, 2u));
      CHECK(stallsIn(kStandardEndpointIn, kGetStatus, 0u, 0u, 2u));
      CHECK(stallsOut(kStandardDeviceOut, kSetAddress, 128u, 0u));
   }

   SUBCASE("in the Address state") {
      REQUIRE(fixture.out(kStandardDeviceOut, kSetAddress, 3u, 0u));
      // A full-speed-only device has no device qualifier and no other-speed configuration.
      CHECK(stallsIn(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::DeviceQualifier), 0u, 10u));
      CHECK(stallsIn(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::OtherSpeedConfiguration), 0u, 255u));
      CHECK(stallsIn(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::Configuration, 1u), 0u, 9u));
      CHECK(stallsIn(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::String, 5u), 0x0409u, 255u));
      CHECK(stallsIn(kStandardDeviceIn, kGetDescriptor, descriptorValue(DescriptorType::Interface), 0u, 9u));
      // BOS: the device reports USB 2.0 and has none.
      CHECK(stallsIn(kStandardDeviceIn, kGetDescriptor, 0x0F00u, 0u, 5u));
      CHECK(stallsOut(kStandardDeviceOut, kSetConfiguration, 2u, 0u));
      CHECK(stallsOut(kStandardDeviceOut, kSetDescriptor, descriptorValue(DescriptorType::Device), 0u));
      // TEST_MODE, and remote wakeup on a configuration that does not declare it.
      CHECK(stallsOut(kStandardDeviceOut, kSetFeature, 2u, 0x0100u));
      CHECK(stallsOut(kStandardDeviceOut, kSetFeature, 1u, 0u));
      // Interfaces and endpoints other than EP0 exist only when configured.
      CHECK(stallsIn(kStandardInterfaceIn, kGetInterface, 0u, 0u, 1u));
      CHECK(stallsIn(kStandardEndpointIn, kGetStatus, 0u, 0x81u, 2u));
      CHECK(stallsIn(kClassInterfaceIn, 0x21u, 0u, 0u, 7u));
      // EP0 itself answers, and cannot be halted.
      CHECK(fixture.in(kStandardEndpointIn, kGetStatus, 0u, 0x80u, 2u).data == std::vector<uint8_t>{ 0u, 0u });
      CHECK(stallsOut(kStandardEndpointOut, kSetFeature, 0u, 0x00u));
      CHECK_FALSE(stallsOut(kStandardEndpointOut, kClearFeature, 0u, 0x00u));
      // Wrong direction.
      CHECK(stallsIn(kStandardDeviceIn, kSetConfiguration, 1u, 0u, 0u));
      CHECK(stallsOut(kStandardDeviceOut, kGetConfiguration, 0u, 0u));
   }

   SUBCASE("in the Configured state") {
      REQUIRE(fixture.out(kStandardDeviceOut, kSetAddress, 3u, 0u));
      REQUIRE(fixture.out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
      CHECK(stallsOut(kStandardDeviceOut, kSetAddress, 4u, 0u));
      CHECK(stallsIn(kStandardInterfaceIn, kGetInterface, 0u, 3u, 1u));
      CHECK(stallsOut(kStandardInterfaceOut, kSetInterface, 2u, 2u));
      CHECK(stallsOut(kStandardInterfaceOut, kSetFeature, 0u, 0u));
      CHECK(stallsIn(kStandardEndpointIn, kGetStatus, 0u, 0x84u, 2u));
      CHECK(stallsIn(kStandardEndpointIn, kSynchFrame, 0u, 0x82u, 2u));
      CHECK(stallsIn(kVendorDeviceIn, 0x01u, 0u, 0u, 4u));
      // A function that does not know the request.
      fixture.msc.accept = false;
      CHECK(stallsIn(kClassInterfaceIn, 0xFEu, 0u, 2u, 1u));
      // A host-to-device data stage that does not fit into the control buffer.
      std::vector<uint8_t> tooLong(Device::kControlBufferSize + 1u, 0x55u);
      const FakeDeviceController::ControlResult result =
      controller.controlOut(setupPacket(kClassInterfaceOut, 0x20u, 0u, 0u, static_cast<uint16_t>(tooLong.size())),
                            art::span<const uint8_t>(tooLong.data(), tooLong.size()));
      CHECK(result.stalled);
      CHECK(fixture.device.state() == DeviceState::Configured);
   }

   // A stall ends with the next SETUP packet: the device answers normally.
   CHECK(controller.isStalled(0x80u));
   CHECK(fixture.getDescriptor(DescriptorType::Device, 0u, 18u).size() == 18u);
}

TEST_CASE("[usb::Device] endpoint halt") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   fixture.enumerate();

   CHECK(fixture.in(kStandardEndpointIn, kGetStatus, 0u, 0x83u, 2u).data == std::vector<uint8_t>{ 0u, 0u });
   CHECK(fixture.out(kStandardEndpointOut, kSetFeature, 0u, 0x83u));
   CHECK(controller.isStalled(0x83u));
   CHECK(fixture.device.isEndpointHalted(0x83u));
   CHECK(fixture.in(kStandardEndpointIn, kGetStatus, 0u, 0x83u, 2u).data == std::vector<uint8_t>{ 1u, 0u });

   SUBCASE("CLEAR_FEATURE ends the halt and resets the data toggle") {
      CHECK(fixture.out(kStandardEndpointOut, kClearFeature, 0u, 0x83u));
      CHECK_FALSE(controller.isStalled(0x83u));
      CHECK_FALSE(fixture.device.isEndpointHalted(0x83u));
      CHECK(controller.toggleResets(0x83u) == 1);
      CHECK(fixture.msc.haltCleared == std::vector<uint8_t>{ 0x83u });
      CHECK(fixture.in(kStandardEndpointIn, kGetStatus, 0u, 0x83u, 2u).data == std::vector<uint8_t>{ 0u, 0u });
   }

   SUBCASE("a function can keep the endpoint halted") {
      fixture.msc.keepHalted = true;
      CHECK(fixture.out(kStandardEndpointOut, kClearFeature, 0u, 0x83u));
      CHECK(controller.isStalled(0x83u));
      CHECK(fixture.device.isEndpointHalted(0x83u));
   }

   SUBCASE("a function halts an endpoint") {
      fixture.device.haltEndpoint(0x02u);
      CHECK(controller.isStalled(0x02u));
      CHECK(fixture.in(kStandardEndpointIn, kGetStatus, 0u, 0x02u, 2u).data == std::vector<uint8_t>{ 1u, 0u });
   }

   SUBCASE("SET_CONFIGURATION clears the halt") {
      CHECK(fixture.out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
      CHECK_FALSE(fixture.device.isEndpointHalted(0x83u));
      CHECK(fixture.msc.deconfiguredCount == 1);
      CHECK(fixture.msc.configuredCount == 2);
   }
}

TEST_CASE("[usb::Device] SET_INTERFACE switches the endpoints of the alternate setting") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   fixture.enumerate();

   CHECK(fixture.in(kStandardInterfaceIn, kGetInterface, 0u, 2u, 1u).data == std::vector<uint8_t>{ 0u });
   CHECK(fixture.out(kStandardInterfaceOut, kSetInterface, 1u, 2u));
   CHECK(fixture.device.alternateSetting(2u) == 1u);
   CHECK(fixture.in(kStandardInterfaceIn, kGetInterface, 0u, 2u, 1u).data == std::vector<uint8_t>{ 1u });
   CHECK_FALSE(controller.isOpen(0x03u));
   CHECK_FALSE(controller.isOpen(0x83u));
   // The other interfaces keep their endpoints.
   CHECK(controller.isOpen(0x02u));
   CHECK(fixture.msc.alternateSettingChanges == std::vector<std::pair<uint8_t, uint8_t>>{ { 2u, 1u } });

   CHECK(fixture.out(kStandardInterfaceOut, kSetInterface, 0u, 2u));
   CHECK(controller.isOpen(0x03u));
   CHECK(controller.isOpen(0x83u));

   SUBCASE("a setting whose endpoints do not fit keeps the old setting") {
      CHECK(fixture.out(kStandardInterfaceOut, kSetInterface, 1u, 2u));
      controller.failOpenOf(0x83u);
      CHECK_FALSE(fixture.out(kStandardInterfaceOut, kSetInterface, 0u, 2u));
      CHECK(fixture.device.alternateSetting(2u) == 1u);
   }
}

TEST_CASE("[usb::Device] SET_CONFIGURATION stalls if an endpoint cannot be opened") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   controller.busReset();
   REQUIRE(fixture.out(kStandardDeviceOut, kSetAddress, 3u, 0u));
   controller.failOpenOf(0x83u);
   CHECK_FALSE(fixture.out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
   CHECK(fixture.device.state() == DeviceState::Address);
   CHECK(controller.openEndpointCount() == 2u);
   CHECK(fixture.msc.configuredCount == 0);
}

TEST_CASE("[usb::Device] class requests go to the function of the interface or endpoint") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   fixture.enumerate();

   SUBCASE("device-to-host") {
      fixture.cdc.reply = { 0x80u, 0x25u, 0x00u, 0x00u, 0x00u, 0x00u, 0x08u };
      const FakeDeviceController::ControlResult result = fixture.in(kClassInterfaceIn, 0x21u, 0u, 0u, 7u);
      CHECK(result.data == fixture.cdc.reply);
      REQUIRE(fixture.cdc.requests.size() == 1u);
      CHECK(fixture.cdc.requests[0].bRequest == 0x21u);
      CHECK(fixture.msc.requests.empty());
   }

   SUBCASE("a reply longer than wLength is cut") {
      fixture.msc.reply = { 1u, 2u, 3u };
      CHECK(fixture.in(kClassInterfaceIn, 0xFEu, 0u, 2u, 1u).data == std::vector<uint8_t>{ 1u });
   }

   SUBCASE("host-to-device with a data stage") {
      const std::array<uint8_t, 7> lineCoding{ 0x00u, 0xC2u, 0x01u, 0x00u, 0x00u, 0x00u, 0x08u };
      bool ok = false;
      const FakeDeviceController::ControlResult result =
      controller.controlOut(setupPacket(kClassInterfaceOut, 0x20u, 0u, 0u, 7u),
                            art::span<const uint8_t>(lineCoding.data(), lineCoding.size()), &ok);
      CHECK(ok);
      CHECK_FALSE(result.stalled);
      CHECK(fixture.cdc.received == std::vector<uint8_t>(lineCoding.begin(), lineCoding.end()));
   }

   SUBCASE("host-to-device without data stage, to the second interface of a function") {
      CHECK(fixture.out(kClassInterfaceOut, 0x22u, 3u, 1u));
      REQUIRE(fixture.cdc.requests.size() == 1u);
      CHECK(fixture.cdc.requests[0].wValue == 3u);
   }

   SUBCASE("endpoint recipient") {
      fixture.msc.reply = { 9u };
      CHECK(fixture.in(kClassEndpointIn, 0x01u, 0u, 0x83u, 1u).data == std::vector<uint8_t>{ 9u });
      REQUIRE(fixture.msc.requests.size() == 1u);
   }
}

TEST_CASE("[usb::Device] multi-packet host-to-device data stage") {
   // The same device with an EP0 packet size of 8.
   std::array<uint8_t, 18> deviceDescriptor = kDeviceDescriptor;
   deviceDescriptor[7] = 8u;
   Fixture fixture(makeDescriptors(art::span<const uint8_t>(deviceDescriptor.data(), deviceDescriptor.size())));
   fixture.enumerate();
   REQUIRE(fixture.controller.endpoint(0x00u)->maxPacketSize == 8u);

   std::vector<uint8_t> data(20u);
   for(size_t index = 0u; index < data.size(); ++index) {
      data[index] = static_cast<uint8_t>(index);
   }
   bool ok = false;
   const FakeDeviceController::ControlResult result =
   fixture.controller.controlOut(setupPacket(kClassInterfaceOut, 0x20u, 0u, 2u, 20u),
                                 art::span<const uint8_t>(data.data(), data.size()), &ok);
   CHECK(ok);
   CHECK_FALSE(result.stalled);
   CHECK(fixture.msc.received == data);

   SUBCASE("a data stage shorter than wLength stalls") {
      fixture.msc.requests.clear();
      fixture.controller.sendSetup(setupPacket(kClassInterfaceOut, 0x20u, 0u, 2u, 20u));
      CHECK(fixture.controller.sendOut(0x00u, art::span<const uint8_t>(data.data(), 8u)));
      CHECK(fixture.controller.sendOut(0x00u, art::span<const uint8_t>(data.data(), 3u)));
      CHECK(fixture.controller.isStalled(0x80u));
      CHECK(fixture.msc.requests.empty());
   }

   // And the device-to-host direction: 16 bytes for wLength 32 end with a ZLP.
   fixture.msc.reply.assign(16u, 0xAAu);
   CHECK(fixture.in(kClassInterfaceIn, 0xFEu, 0u, 2u, 32u).packetSizes == std::vector<size_t>{ 8u, 8u, 0u });
}

TEST_CASE("[usb::Device] data endpoints are routed to their function") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   fixture.enumerate();

   fixture.device.armOutTransfer(0x02u);
   const std::array<uint8_t, 3> packet{ 1u, 2u, 3u };
   CHECK(controller.sendOut(0x02u, art::span<const uint8_t>(packet.data(), packet.size())));
   REQUIRE(fixture.cdc.outPackets.size() == 1u);
   CHECK(fixture.cdc.outPackets[0].first == 0x02u);
   CHECK(fixture.cdc.outPackets[0].second == std::vector<uint8_t>{ 1u, 2u, 3u });

   CHECK(fixture.device.startInTransfer(0x83u, art::span<const uint8_t>(packet.data(), packet.size())));
   CHECK(controller.takeIn(0x83u).has_value());
   CHECK(fixture.msc.inCompletions == std::vector<uint8_t>{ 0x83u });
   CHECK(fixture.cdc.inCompletions.empty());

   // EP0 and closed endpoints are not available to functions.
   CHECK_FALSE(fixture.device.startInTransfer(0x80u, art::span<const uint8_t>()));
   CHECK_FALSE(fixture.device.startInTransfer(0x84u, art::span<const uint8_t>()));
}

TEST_CASE("[usb::Device] suspend, resume and bus reset") {
   Fixture fixture;
   FakeDeviceController &controller = fixture.controller;
   fixture.enumerate();

   controller.busSuspend();
   CHECK(fixture.device.state() == DeviceState::Suspended);
   CHECK(fixture.device.isConfigured());
   CHECK(fixture.cdc.suspendedCount == 1);
   controller.busSuspend();
   CHECK(fixture.cdc.suspendedCount == 1);

   SUBCASE("resume returns to the state before") {
      controller.busResume();
      CHECK(fixture.device.state() == DeviceState::Configured);
      CHECK(fixture.cdc.resumedCount == 1);
   }

   SUBCASE("a bus reset ends the suspend and the configuration") {
      controller.busReset();
      CHECK(fixture.device.state() == DeviceState::Default);
      CHECK_FALSE(fixture.device.isConfigured());
      CHECK(fixture.device.address() == 0u);
      CHECK(fixture.cdc.resumedCount == 1);
      CHECK(fixture.cdc.deconfiguredCount == 1);
      CHECK(fixture.msc.deconfiguredCount == 1);
      // Only EP0 is open again.
      CHECK(controller.openEndpointCount() == 2u);
   }
}

TEST_CASE("[usb::Device] addFunction checks the interface ranges") {
   FakeDeviceController controller;
   Device device(controller, makeDescriptors());
   RecordingFunction first(0u, 2u);
   RecordingFunction overlapping(1u, 1u);
   RecordingFunction empty(3u, 0u);
   RecordingFunction tooHigh(static_cast<uint8_t>(Device::kMaxInterfaces - 1u), 2u);
   RecordingFunction second(2u, 1u);

   CHECK(device.addFunction(first));
   CHECK_FALSE(device.addFunction(first));
   CHECK_FALSE(device.addFunction(overlapping));
   CHECK_FALSE(device.addFunction(empty));
   CHECK_FALSE(device.addFunction(tooHigh));
   CHECK(device.addFunction(second));
}
