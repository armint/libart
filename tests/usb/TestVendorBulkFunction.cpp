#include "FakeDeviceController.h"
#include "usb/CdcAcmFunction.h"
#include "usb/DescriptorBuilder.h"
#include "usb/Device.h"
#include "usb/VendorBulkFunction.h"
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <optional>
#include <vector>

// usb::VendorBulkFunction through usb::Device and the fake controller. The device is laid out as a composite device
// with a vendor interface and a CDC-ACM port: interface 0 vendor-specific (bulk 0x03 OUT and 0x82 IN), interfaces 1
// and 2 CDC-ACM (0x81 notification, 0x01/0x83 bulk). The data tests run twice: with the events delivered at once, and
// with the events queued until the main loop polls.

using namespace art::usb;
using namespace art::usb::descriptor;
using art::usb::test::FakeDeviceController;
using art::usb::test::setupPacket;

namespace {

constexpr uint8_t kStandardDeviceOut = 0x00u;
constexpr uint8_t kStandardInterfaceOut = 0x01u;
constexpr uint8_t kStandardEndpointOut = 0x02u;
constexpr uint8_t kVendorInterfaceIn = 0xC1u;
constexpr uint8_t kClearFeature = 1u;
constexpr uint8_t kSetFeature = 3u;
constexpr uint8_t kSetAddress = 5u;
constexpr uint8_t kSetConfiguration = 9u;
constexpr uint8_t kSetInterface = 11u;

constexpr uint8_t kVendorOut = 0x03u;
constexpr uint8_t kVendorIn = 0x82u;
constexpr uint8_t kCdcNotification = 0x81u;
constexpr uint8_t kCdcOut = 0x01u;
constexpr uint8_t kCdcIn = 0x83u;
constexpr size_t kPacketSize = 64u;

constexpr DeviceFields makeDeviceFields() {
   DeviceFields fields{};
   fields.bDeviceClass = classcode::kMiscellaneous;
   fields.bDeviceSubClass = classcode::kMiscellaneousSubclassCommon;
   fields.bDeviceProtocol = classcode::kMiscellaneousProtocolIad;
   fields.idVendor = 0x1234u;
   fields.idProduct = 0x5678u;
   return fields;
}

constexpr auto kDevice = device(makeDeviceFields());

constexpr auto kConfiguration =
configuration(ConfigurationFields{},
              interfaceDescriptor(InterfaceFields{ 0u, 0u, classcode::kVendorSpecific, 0u, 0u, 0u },
                                  endpoint(kVendorOut, EndpointType::Bulk, kPacketSize),
                                  endpoint(kVendorIn, EndpointType::Bulk, kPacketSize)),
              interfaceAssociation(1u, 2u, classcode::kCdc, cdc::kSubclassAcm, 0u),
              interfaceDescriptor(InterfaceFields{ 1u, 0u, classcode::kCdc, cdc::kSubclassAcm, 0u, 0u },
                                  cdcHeader(),
                                  cdcCallManagement(0u, 2u),
                                  cdcAcm(),
                                  cdcUnion(1u, 2u),
                                  endpoint(kCdcNotification, EndpointType::Interrupt, 16u, 16u)),
              interfaceDescriptor(InterfaceFields{ 2u, 0u, classcode::kCdcData, 0u, 0u, 0u },
                                  endpoint(kCdcOut, EndpointType::Bulk, 64u),
                                  endpoint(kCdcIn, EndpointType::Bulk, 64u)));

/// A test protocol: records the OUT packets and the hook calls, and holds the OUT endpoint on request.
class RecordingFunction final : public VendorBulkFunction {
   public:
   RecordingFunction()
      : VendorBulkFunction(0u, VendorBulkEndpoints{ kVendorOut, kVendorIn, kPacketSize }) {
   }

   using VendorBulkFunction::device;

   /// A vendor request with a device-to-host data stage: replies with the bytes 0, 1, 2, ...
   std::optional<uint16_t> controlRequest(const SetupPacket &setup, art::span<uint8_t> data) override {
      if(setup.type() != RequestType::Vendor || setup.direction() != RequestDirection::DeviceToHost) {
         return std::nullopt;
      }
      for(size_t index = 0u; index < data.size(); ++index) {
         data[index] = static_cast<uint8_t>(index);
      }
      return static_cast<uint16_t>(data.size());
   }

   std::vector<std::vector<uint8_t>> packets;
   /// dataReceived() returns this.
   bool accept{ true };
   int transfersSent{ 0 };
   int opened{ 0 };
   int closed{ 0 };
   /// If not empty, transferSent() starts a transfer of it, once. It must stay unchanged until that transfer is sent.
   std::vector<uint8_t> chainedTransfer;

   protected:
   bool dataReceived(art::span<const uint8_t> packet) override {
      packets.emplace_back(packet.begin(), packet.end());
      return accept;
   }

   void transferSent() override {
      CHECK_FALSE(isInTransferBusy());
      ++transfersSent;
      if(!chainedTransfer.empty() && !chained_) {
         chained_ = true;
         CHECK(sendInTransfer(art::span<const uint8_t>(chainedTransfer.data(), chainedTransfer.size())));
      }
   }

   void dataPathOpened() override {
      CHECK(isConfigured());
      ++opened;
   }

   void dataPathClosed() override {
      CHECK_FALSE(isConfigured());
      CHECK_FALSE(isInTransferBusy());
      ++closed;
   }

   private:
   bool chained_{ false };
};

/// The composite device, connected but not yet enumerated.
class Bench {
   public:
   explicit Bench(bool queueEvents)
      : descriptors_{ asSpan(kDevice), asSpan(kConfiguration), art::span<const art::span<const uint8_t>>() }
      , device_(controller_, descriptors_) {
      REQUIRE(device_.addFunction(vendor));
      REQUIRE(device_.addFunction(cdc));
      controller_.queueEvents(queueEvents);
      device_.start();
   }

   FakeDeviceController &controller() {
      return controller_;
   }

   /// Bus reset, SET_ADDRESS 5 and SET_CONFIGURATION 1.
   void enumerate() {
      controller_.busReset();
      CHECK(out(kStandardDeviceOut, kSetAddress, 5u, 0u));
      CHECK(out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
      poll();
      REQUIRE(device_.state() == DeviceState::Configured);
      REQUIRE(vendor.isConfigured());
   }

   /// The main loop: delivers the queued events.
   void poll() {
      controller_.deliverQueuedEvents();
   }

   /// A host-to-device request without data stage. Returns false if the device stalled it.
   bool out(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index) {
      bool ok = false;
      const FakeDeviceController::ControlResult result =
      controller_.controlOut(setupPacket(requestType, request, value, index, 0u), art::span<const uint8_t>(), &ok);
      CHECK(ok);
      poll();
      return !result.stalled;
   }

   /// The host sends one bulk OUT packet after the main loop ran. Returns false if the endpoint NAKs.
   bool sendOut(uint8_t endpointAddress, const std::vector<uint8_t> &packet) {
      poll();
      const bool sent = controller_.sendOut(endpointAddress, art::span<const uint8_t>(packet.data(), packet.size()));
      poll();
      return sent;
   }

   /// The host reads one IN packet after the main loop ran, or nullopt if the endpoint NAKs.
   std::optional<std::vector<uint8_t>> takeIn(uint8_t endpointAddress) {
      poll();
      std::optional<std::vector<uint8_t>> packet = controller_.takeIn(endpointAddress);
      poll();
      return packet;
   }

   /// The host reads IN packets until a short packet (or a ZLP) ends the transfer, or the endpoint NAKs.
   std::vector<size_t> readTransfer(uint8_t endpointAddress, std::vector<uint8_t> &data) {
      std::vector<size_t> packetSizes;
      while(std::optional<std::vector<uint8_t>> packet = takeIn(endpointAddress)) {
         packetSizes.push_back(packet->size());
         data.insert(data.end(), packet->begin(), packet->end());
         if(packet->size() < kPacketSize) {
            break;
         }
      }
      return packetSizes;
   }

   RecordingFunction vendor;
   CdcAcmFunction<128u, 128u> cdc{ 1u, CdcAcmEndpoints{ kCdcNotification, 16u, kCdcOut, kCdcIn } };

   private:
   Descriptors descriptors_;
   FakeDeviceController controller_;
   Device device_;
};

std::vector<uint8_t> pattern(size_t length, uint8_t start = 0u) {
   std::vector<uint8_t> data(length);
   for(size_t index = 0u; index < length; ++index) {
      data[index] = static_cast<uint8_t>(start + index);
   }
   return data;
}

art::span<const uint8_t> asBytes(const std::vector<uint8_t> &data) {
   return art::span<const uint8_t>(data.data(), data.size());
}

} // namespace

TEST_CASE("[usb::VendorBulkFunction] IN transfers are split into packets and end with a ZLP if needed") {
   using Zlp = VendorBulkFunction::ZeroLengthPacket;
   struct Case {
      size_t length;
      Zlp zlp;
      std::vector<size_t> packetSizes;
   };
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();
      int transfers = 0;
      for(const Case &testCase :
          { Case{ 1u, Zlp::Auto, { 1u } }, Case{ 63u, Zlp::Auto, { 63u } }, Case{ 64u, Zlp::Auto, { 64u, 0u } },
            Case{ 65u, Zlp::Auto, { 64u, 1u } }, Case{ 128u, Zlp::Auto, { 64u, 64u, 0u } },
            Case{ 200u, Zlp::Auto, { 64u, 64u, 64u, 8u } }, Case{ 64u, Zlp::Never, { 64u } }, Case{ 128u, Zlp::Never, { 64u, 64u } },
            Case{ 100u, Zlp::Never, { 64u, 36u } }, Case{ 0u, Zlp::Auto, { 0u } }, Case{ 0u, Zlp::Never, { 0u } } }) {
         CAPTURE(testCase.length);
         CAPTURE(testCase.zlp == Zlp::Never);
         const std::vector<uint8_t> data = pattern(testCase.length, static_cast<uint8_t>(testCase.length));
         REQUIRE(bench.vendor.sendInTransfer(asBytes(data), testCase.zlp));
         CHECK(bench.vendor.isInTransferBusy());
         // One transfer at a time.
         CHECK_FALSE(bench.vendor.sendInTransfer(asBytes(data)));

         std::vector<uint8_t> received;
         std::vector<size_t> packetSizes;
         while(std::optional<std::vector<uint8_t>> packet = bench.takeIn(kVendorIn)) {
            // The transfer is complete only after its last packet.
            CHECK(bench.vendor.transfersSent == (bench.vendor.isInTransferBusy() ? transfers : transfers + 1));
            packetSizes.push_back(packet->size());
            received.insert(received.end(), packet->begin(), packet->end());
         }
         CHECK(packetSizes == testCase.packetSizes);
         CHECK(received == data);
         CHECK(bench.vendor.transfersSent == ++transfers);
         CHECK_FALSE(bench.vendor.isInTransferBusy());
      }
   }
}

TEST_CASE("[usb::VendorBulkFunction] transferSent() can start the next transfer") {
   Bench bench(false);
   bench.enumerate();
   const std::vector<uint8_t> first = pattern(64u);
   bench.vendor.chainedTransfer = pattern(70u, 100u);
   REQUIRE(bench.vendor.sendInTransfer(asBytes(first)));
   std::vector<uint8_t> received;
   CHECK(bench.readTransfer(kVendorIn, received) == std::vector<size_t>{ 64u, 0u });
   // The hook started the second transfer.
   CHECK(bench.vendor.transfersSent == 1);
   CHECK(bench.vendor.isInTransferBusy());
   received.clear();
   CHECK(bench.readTransfer(kVendorIn, received) == std::vector<size_t>{ 64u, 6u });
   CHECK(received == pattern(70u, 100u));
   CHECK(bench.vendor.transfersSent == 2);
   CHECK_FALSE(bench.takeIn(kVendorIn).has_value());
}

TEST_CASE("[usb::VendorBulkFunction] OUT packets reach dataReceived()") {
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();
      CHECK(bench.vendor.opened == 1);
      CHECK(bench.controller().isArmed(kVendorOut));

      REQUIRE(bench.sendOut(kVendorOut, pattern(64u)));
      REQUIRE(bench.sendOut(kVendorOut, pattern(10u, 64u)));
      REQUIRE(bench.sendOut(kVendorOut, {}));
      CHECK(bench.vendor.packets == std::vector<std::vector<uint8_t>>{ pattern(64u), pattern(10u, 64u), {} });
      CHECK(bench.controller().isArmed(kVendorOut));
   }
}

TEST_CASE("[usb::VendorBulkFunction] a held OUT endpoint NAKs until resumeOut()") {
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();
      bench.vendor.accept = false;
      REQUIRE(bench.sendOut(kVendorOut, pattern(5u)));
      CHECK(bench.vendor.isOutHeld());
      CHECK_FALSE(bench.sendOut(kVendorOut, pattern(5u, 5u)));
      CHECK(bench.vendor.packets.size() == 1u);

      // The main loop processed the command: the host can send the next one.
      bench.vendor.accept = true;
      bench.vendor.resumeOut();
      CHECK_FALSE(bench.vendor.isOutHeld());
      REQUIRE(bench.sendOut(kVendorOut, pattern(5u, 5u)));
      CHECK(bench.vendor.packets == std::vector<std::vector<uint8_t>>{ pattern(5u), pattern(5u, 5u) });

      // resumeOut() without a hold changes nothing.
      bench.vendor.resumeOut();
      CHECK(bench.controller().isArmed(kVendorOut));
   }
}

TEST_CASE("[usb::VendorBulkFunction] request and reply, as a command processor in the main loop does") {
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();
      bench.vendor.accept = false;
      for(const size_t length : { 1u, 64u, 130u, 512u }) {
         CAPTURE(length);
         REQUIRE(bench.sendOut(kVendorOut, pattern(4u)));
         // The main loop: process the held command, reply, accept the next command.
         const std::vector<uint8_t> reply = pattern(length, 7u);
         REQUIRE(bench.vendor.sendInTransfer(asBytes(reply)));
         bench.vendor.resumeOut();
         std::vector<uint8_t> received;
         bench.readTransfer(kVendorIn, received);
         CHECK(received == reply);
         CHECK_FALSE(bench.takeIn(kVendorIn).has_value());
      }
      CHECK(bench.vendor.packets.size() == 4u);
   }
}

TEST_CASE("[usb::VendorBulkFunction] no data path before the configuration") {
   Bench bench(false);
   const std::vector<uint8_t> data = pattern(10u);
   CHECK_FALSE(bench.vendor.isConfigured());
   CHECK_FALSE(bench.vendor.sendInTransfer(asBytes(data)));
   CHECK(bench.vendor.opened == 0);
   bench.enumerate();
   CHECK(bench.vendor.sendInTransfer(asBytes(data)));
   CHECK(bench.takeIn(kVendorIn) == data);
}

TEST_CASE("[usb::VendorBulkFunction] deconfiguration drops the transfer and the hold") {
   Bench bench(false);
   bench.enumerate();
   const std::vector<uint8_t> data = pattern(200u);
   bench.vendor.accept = false;
   REQUIRE(bench.sendOut(kVendorOut, pattern(3u)));
   REQUIRE(bench.vendor.sendInTransfer(asBytes(data)));
   REQUIRE(bench.takeIn(kVendorIn).has_value());

   SUBCASE("bus reset") {
      bench.controller().busReset();
   }
   SUBCASE("SET_CONFIGURATION 0") {
      CHECK(bench.out(kStandardDeviceOut, kSetConfiguration, 0u, 0u));
   }
   CHECK(bench.vendor.closed == 1);
   CHECK_FALSE(bench.vendor.isConfigured());
   CHECK_FALSE(bench.vendor.isInTransferBusy());
   CHECK_FALSE(bench.vendor.isOutHeld());
   CHECK(bench.vendor.transfersSent == 0);

   bench.enumerate();
   CHECK(bench.vendor.opened == 2);
   CHECK_FALSE(bench.takeIn(kVendorIn).has_value());
   CHECK(bench.controller().isArmed(kVendorOut));
   REQUIRE(bench.vendor.sendInTransfer(asBytes(data)));
   std::vector<uint8_t> received;
   bench.readTransfer(kVendorIn, received);
   CHECK(received == data);
}

TEST_CASE("[usb::VendorBulkFunction] the transfer continues after an endpoint halt") {
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();
      const std::vector<uint8_t> data = pattern(150u);
      REQUIRE(bench.vendor.sendInTransfer(asBytes(data)));
      std::vector<uint8_t> received = *bench.takeIn(kVendorIn);

      SUBCASE("SET_FEATURE and CLEAR_FEATURE of the host") {
         // The second packet waits in the controller. The halt drops it; it goes out again after the clear.
         CHECK(bench.out(kStandardEndpointOut, kSetFeature, 0u, kVendorIn));
         CHECK_FALSE(bench.takeIn(kVendorIn).has_value());
         CHECK(bench.vendor.isInTransferBusy());
         CHECK(bench.out(kStandardEndpointOut, kClearFeature, 0u, kVendorIn));
      }
      SUBCASE("halt by the function, CLEAR_FEATURE of the host") {
         bench.vendor.device()->haltEndpoint(kVendorIn);
         bench.poll();
         CHECK_FALSE(bench.takeIn(kVendorIn).has_value());
         CHECK(bench.out(kStandardEndpointOut, kClearFeature, 0u, kVendorIn));
      }
      SUBCASE("SET_INTERFACE") {
         CHECK(bench.out(kStandardInterfaceOut, kSetInterface, 0u, 0u));
      }
      SUBCASE("CLEAR_FEATURE of an endpoint that is not halted") {
         CHECK(bench.out(kStandardEndpointOut, kClearFeature, 0u, kVendorIn));
      }

      bench.readTransfer(kVendorIn, received);
      CHECK(received == data);
      CHECK(bench.vendor.transfersSent == 1);
      CHECK_FALSE(bench.takeIn(kVendorIn).has_value());
   }
}

TEST_CASE("[usb::VendorBulkFunction] the OUT endpoint is armed again after a halt, unless it is held") {
   Bench bench(false);
   bench.enumerate();

   SUBCASE("not held") {
      CHECK(bench.out(kStandardEndpointOut, kSetFeature, 0u, kVendorOut));
      CHECK_FALSE(bench.sendOut(kVendorOut, pattern(3u)));
      CHECK(bench.out(kStandardEndpointOut, kClearFeature, 0u, kVendorOut));
      REQUIRE(bench.sendOut(kVendorOut, pattern(3u)));
      CHECK(bench.vendor.packets.size() == 1u);
   }

   SUBCASE("held") {
      bench.vendor.accept = false;
      REQUIRE(bench.sendOut(kVendorOut, pattern(3u)));
      CHECK(bench.out(kStandardEndpointOut, kClearFeature, 0u, kVendorOut));
      CHECK(bench.out(kStandardInterfaceOut, kSetInterface, 0u, 0u));
      CHECK_FALSE(bench.controller().isArmed(kVendorOut));
      bench.vendor.resumeOut();
      CHECK(bench.controller().isArmed(kVendorOut));
   }

   SUBCASE("resumeOut() on a halted endpoint arms it after the clear") {
      bench.vendor.accept = false;
      REQUIRE(bench.sendOut(kVendorOut, pattern(3u)));
      CHECK(bench.out(kStandardEndpointOut, kSetFeature, 0u, kVendorOut));
      bench.vendor.resumeOut();
      CHECK_FALSE(bench.controller().isArmed(kVendorOut));
      CHECK(bench.out(kStandardEndpointOut, kClearFeature, 0u, kVendorOut));
      CHECK(bench.controller().isArmed(kVendorOut));
   }
}

TEST_CASE("[usb::VendorBulkFunction] vendor requests reach the subclass") {
   Bench bench(false);
   bench.enumerate();
   bool ok = false;
   const FakeDeviceController::ControlResult result =
   bench.controller().controlIn(setupPacket(kVendorInterfaceIn, 0x42u, 0u, 0u, 5u), &ok);
   CHECK(ok);
   CHECK_FALSE(result.stalled);
   CHECK(result.data == pattern(5u));
}

TEST_CASE("[usb::VendorBulkFunction] works next to a CDC-ACM function") {
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();
      CHECK(bench.cdc.isConfigured());

      // Interleaved traffic on both functions reaches the right one.
      REQUIRE(bench.sendOut(kVendorOut, pattern(8u)));
      REQUIRE(bench.sendOut(kCdcOut, pattern(5u, 50u)));
      const std::vector<uint8_t> reply = pattern(70u, 1u);
      REQUIRE(bench.vendor.sendInTransfer(asBytes(reply)));
      const std::vector<uint8_t> serial = pattern(9u, 90u);
      REQUIRE(bench.cdc.write(asBytes(serial)) == serial.size());

      CHECK(bench.takeIn(kCdcIn) == serial);
      std::vector<uint8_t> received;
      bench.readTransfer(kVendorIn, received);
      CHECK(received == reply);
      CHECK(bench.vendor.packets == std::vector<std::vector<uint8_t>>{ pattern(8u) });
      std::array<uint8_t, 16> buffer{};
      CHECK(bench.cdc.read(art::span<uint8_t>(buffer.data(), buffer.size())) == 5u);
      CHECK(std::vector<uint8_t>(buffer.begin(), buffer.begin() + 5) == pattern(5u, 50u));
   }
}
