#include "FakeDeviceController.h"
#include "usb/CdcAcmFunction.h"
#include "usb/DescriptorBuilder.h"
#include "usb/Device.h"
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <optional>
#include <vector>

// usb::CdcAcmFunction through usb::Device and the fake controller: a CDC-only device (interface 0 communication,
// interface 1 data; endpoint 0x81 notification with 8-byte packets, 0x02/0x82 bulk data). The data tests run twice:
// with the events delivered at once, and with the events queued until the main loop polls.

using namespace art::usb;
using namespace art::usb::descriptor;
using art::usb::test::FakeDeviceController;
using art::usb::test::setupPacket;

namespace {

constexpr uint8_t kStandardDeviceOut = 0x00u;
constexpr uint8_t kStandardInterfaceOut = 0x01u;
constexpr uint8_t kClassInterfaceIn = 0xA1u;
constexpr uint8_t kClassInterfaceOut = 0x21u;
constexpr uint8_t kSetAddress = 5u;
constexpr uint8_t kSetConfiguration = 9u;
constexpr uint8_t kSetInterface = 11u;
constexpr uint8_t kSendBreak = 0x23u;

constexpr uint8_t kNotification = 0x81u;
constexpr uint8_t kDataOut = 0x02u;
constexpr uint8_t kDataIn = 0x82u;

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
              interfaceAssociation(0u, 2u, classcode::kCdc, cdc::kSubclassAcm, 0u),
              interfaceDescriptor(InterfaceFields{ 0u, 0u, classcode::kCdc, cdc::kSubclassAcm, 0u, 0u },
                                  cdcHeader(),
                                  cdcCallManagement(0u, 1u),
                                  cdcAcm(),
                                  cdcUnion(0u, 1u),
                                  endpoint(kNotification, EndpointType::Interrupt, 8u, 16u)),
              interfaceDescriptor(InterfaceFields{ 1u, 0u, classcode::kCdcData, 0u, 0u, 0u },
                                  endpoint(kDataOut, EndpointType::Bulk, 64u),
                                  endpoint(kDataIn, EndpointType::Bulk, 64u)));

constexpr CdcAcmEndpoints kEndpoints{ kNotification, 8u, kDataOut, kDataIn };

/// RX queue of two packets, TX queue of four packets.
using TestCdcAcm = CdcAcmFunction<128u, 256u>;

/// A device with only the CDC-ACM function, connected but not yet enumerated.
class Bench {
   public:
   explicit Bench(bool queueEvents)
      : descriptors_{ asSpan(kDevice), asSpan(kConfiguration), art::span<const art::span<const uint8_t>>() }
      , device_(controller_, descriptors_) {
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
      REQUIRE(cdc.isConfigured());
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
      return !result.stalled;
   }

   /// A host-to-device request with a data stage. Returns false if the device stalled it.
   bool outData(uint8_t request, uint16_t index, const std::vector<uint8_t> &data) {
      bool ok = false;
      const FakeDeviceController::ControlResult result =
      controller_.controlOut(setupPacket(kClassInterfaceOut, request, 0u, index, static_cast<uint16_t>(data.size())),
                             art::span<const uint8_t>(data.data(), data.size()), &ok);
      CHECK(ok);
      return !result.stalled;
   }

   /// A class request to an interface with a device-to-host data stage.
   FakeDeviceController::ControlResult in(uint8_t request, uint16_t index, uint16_t length) {
      bool ok = false;
      FakeDeviceController::ControlResult result =
      controller_.controlIn(setupPacket(kClassInterfaceIn, request, 0u, index, length), &ok);
      CHECK(ok);
      return result;
   }

   /// The host sends one bulk OUT packet after the main loop ran. Returns false if the endpoint NAKs.
   bool sendOut(const std::vector<uint8_t> &packet) {
      poll();
      const bool sent = controller_.sendOut(kDataOut, art::span<const uint8_t>(packet.data(), packet.size()));
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

   /// The host reads bulk IN packets until a short packet (or a ZLP) ends the transfer, or the endpoint NAKs.
   std::vector<size_t> readTransfer(std::vector<uint8_t> &data) {
      std::vector<size_t> packetSizes;
      while(std::optional<std::vector<uint8_t>> packet = takeIn(kDataIn)) {
         packetSizes.push_back(packet->size());
         data.insert(data.end(), packet->begin(), packet->end());
         if(packet->size() < CdcAcmFunctionBase::kDataPacketSize) {
            break;
         }
      }
      return packetSizes;
   }

   TestCdcAcm cdc{ 0u, kEndpoints };

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

/// Reads everything that the function received.
std::vector<uint8_t> readAll(CdcAcmFunctionBase &cdc) {
   std::vector<uint8_t> data(cdc.readAvailable());
   CHECK(cdc.read(art::span<uint8_t>(data.data(), data.size())) == data.size());
   return data;
}

} // namespace

TEST_CASE("[usb::CdcAcmFunction] loopback through the fake controller") {
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();
      CHECK(bench.controller().isArmed(kDataOut));

      // An echo in the main loop: the host sends a transfer, the firmware writes back what it read.
      for(const size_t length : { 1u, 63u, 64u, 65u, 127u, 128u, 200u }) {
         CAPTURE(length);
         const std::vector<uint8_t> sent = pattern(length, static_cast<uint8_t>(length));
         std::vector<uint8_t> echoed;
         size_t offset = 0u;
         while(offset < sent.size()) {
            const size_t packetLength = std::min<size_t>(64u, sent.size() - offset);
            REQUIRE(bench.sendOut(std::vector<uint8_t>(sent.begin() + offset, sent.begin() + offset + packetLength)));
            offset += packetLength;
            const std::vector<uint8_t> received = readAll(bench.cdc);
            CHECK(bench.cdc.write(art::span<const uint8_t>(received.data(), received.size())) == received.size());
         }
         // A transfer of a multiple of 64 bytes ends with a ZLP from the host too; the function ignores the empty
         // packet.
         if(length % 64u == 0u) {
            REQUIRE(bench.sendOut({}));
            CHECK(bench.cdc.readAvailable() == 0u);
         }
         while(echoed.size() < sent.size()) {
            const size_t before = echoed.size();
            bench.readTransfer(echoed);
            REQUIRE(echoed.size() > before);
         }
         CHECK(echoed == sent);
         CHECK_FALSE(bench.takeIn(kDataIn).has_value());
         CHECK(bench.cdc.isTxIdle());
      }
   }
}

TEST_CASE("[usb::CdcAcmFunction] TX ends with a ZLP only after a full last packet") {
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();

      struct Case {
         size_t length;
         std::vector<size_t> packetSizes;
      };
      for(const Case &testCase :
          { Case{ 64u, { 64u, 0u } }, Case{ 128u, { 64u, 64u, 0u } }, Case{ 256u, { 64u, 64u, 64u, 64u, 0u } },
            Case{ 1u, { 1u } }, Case{ 100u, { 64u, 36u } }, Case{ 191u, { 64u, 64u, 63u } } }) {
         CAPTURE(testCase.length);
         const std::vector<uint8_t> data = pattern(testCase.length);
         REQUIRE(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == data.size());
         CHECK_FALSE(bench.cdc.isTxIdle());
         std::vector<uint8_t> received;
         CHECK(bench.readTransfer(received) == testCase.packetSizes);
         CHECK(received == data);
         // Nothing follows the end of the transfer.
         CHECK_FALSE(bench.takeIn(kDataIn).has_value());
         CHECK(bench.cdc.isTxIdle());
      }
   }
}

TEST_CASE("[usb::CdcAcmFunction] a write while a full packet is on its way continues the transfer") {
   Bench bench(false);
   bench.enumerate();
   const std::vector<uint8_t> first = pattern(64u);
   const std::vector<uint8_t> second = pattern(10u, 64u);
   REQUIRE(bench.cdc.write(art::span<const uint8_t>(first.data(), first.size())) == 64u);
   REQUIRE(bench.cdc.write(art::span<const uint8_t>(second.data(), second.size())) == 10u);
   std::vector<uint8_t> received;
   CHECK(bench.readTransfer(received) == std::vector<size_t>{ 64u, 10u });
   CHECK(received == pattern(74u));
   CHECK_FALSE(bench.takeIn(kDataIn).has_value());
}

TEST_CASE("[usb::CdcAcmFunction] write takes only what fits into the TX queue") {
   Bench bench(false);
   bench.enumerate();
   // The queue takes 256 bytes. The first packet then leaves it, which makes room for 64 more.
   const std::vector<uint8_t> data = pattern(400u);
   CHECK(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == 256u);
   CHECK(bench.cdc.writeAvailable() == 64u);
   CHECK(bench.cdc.write(art::span<const uint8_t>(data.data() + 256, 144u)) == 64u);
   CHECK(bench.cdc.writeAvailable() == 0u);
   CHECK(bench.cdc.write(art::span<const uint8_t>(data.data() + 320, 1u)) == 0u);
   std::vector<uint8_t> received;
   bench.readTransfer(received);
   CHECK(received == std::vector<uint8_t>(data.begin(), data.begin() + 320));
}

TEST_CASE("[usb::CdcAcmFunction] the OUT endpoint NAKs while the RX queue has no room for a full packet") {
   for(const bool queueEvents : { false, true }) {
      CAPTURE(queueEvents);
      Bench bench(queueEvents);
      bench.enumerate();

      // The RX queue holds 128 bytes: two full packets fill it.
      REQUIRE(bench.sendOut(pattern(64u)));
      CHECK(bench.controller().isArmed(kDataOut));
      REQUIRE(bench.sendOut(pattern(64u, 64u)));
      CHECK_FALSE(bench.controller().isArmed(kDataOut));
      CHECK_FALSE(bench.sendOut(pattern(64u, 128u)));
      CHECK(bench.cdc.readAvailable() == 128u);

      // Room for less than a packet: still NAK.
      std::array<uint8_t, 63> buffer{};
      CHECK(bench.cdc.read(art::span<uint8_t>(buffer.data(), buffer.size())) == 63u);
      CHECK(buffer[0] == 0u);
      CHECK(buffer[62] == 62u);
      bench.poll();
      CHECK_FALSE(bench.sendOut(pattern(64u, 128u)));

      // Room for a full packet: the host can send again, and no byte is lost.
      CHECK(bench.cdc.read(art::span<uint8_t>(buffer.data(), 1u)) == 1u);
      CHECK(buffer[0] == 63u);
      REQUIRE(bench.sendOut(pattern(64u, 128u)));
      CHECK_FALSE(bench.controller().isArmed(kDataOut));
      CHECK(readAll(bench.cdc) == pattern(128u, 64u));
      CHECK(bench.controller().isArmed(kDataOut));
   }
}

TEST_CASE("[usb::CdcAcmFunction] SET_CONTROL_LINE_STATE sets DTR and RTS") {
   Bench bench(false);
   bench.enumerate();
   CHECK_FALSE(bench.cdc.isTerminalOpen());

   CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, 0x0003u, 0u));
   CHECK(bench.cdc.isTerminalOpen());
   CHECK(bench.cdc.controlLineState() == 0x0003u);

   CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, 0x0000u, 0u));
   CHECK_FALSE(bench.cdc.isTerminalOpen());

   // RTS alone does not open the terminal.
   CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, 0x0002u, 0u));
   CHECK_FALSE(bench.cdc.isTerminalOpen());
   CHECK(bench.cdc.controlLineState() == 0x0002u);

   CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, 0x0001u, 0u));
   CHECK(bench.cdc.isTerminalOpen());

   SUBCASE("a bus reset closes the terminal") {
      bench.controller().busReset();
      CHECK_FALSE(bench.cdc.isTerminalOpen());
      CHECK(bench.cdc.controlLineState() == 0u);
   }

   SUBCASE("SET_CONFIGURATION 0 closes the terminal") {
      CHECK(bench.out(kStandardDeviceOut, kSetConfiguration, 0u, 0u));
      CHECK_FALSE(bench.cdc.isTerminalOpen());
   }

   SUBCASE("the request to the data interface stalls") {
      CHECK_FALSE(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, 0x0000u, 1u));
      CHECK(bench.cdc.isTerminalOpen());
   }
}

TEST_CASE("[usb::CdcAcmFunction] SET_LINE_CODING is stored and returned by GET_LINE_CODING") {
   Bench bench(false);
   bench.enumerate();

   // The default: 115200 baud, 1 stop bit, no parity, 8 data bits.
   FakeDeviceController::ControlResult result = bench.in(CdcAcmFunctionBase::kGetLineCoding, 0u, 7u);
   CHECK_FALSE(result.stalled);
   CHECK(result.data == std::vector<uint8_t>{ 0x00u, 0xC2u, 0x01u, 0x00u, 0u, 0u, 8u });

   // 921600 baud, 2 stop bits, even parity, 7 data bits.
   const std::vector<uint8_t> coding{ 0x00u, 0x10u, 0x0Eu, 0x00u, 2u, 2u, 7u };
   CHECK(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 0u, coding));
   CHECK(bench.cdc.lineCoding().dwDTERate == 921600u);
   CHECK(bench.cdc.lineCoding().bCharFormat == 2u);
   CHECK(bench.cdc.lineCoding().bParityType == 2u);
   CHECK(bench.cdc.lineCoding().bDataBits == 7u);
   result = bench.in(CdcAcmFunctionBase::kGetLineCoding, 0u, 7u);
   CHECK(result.data == coding);

   SUBCASE("a shorter wLength gets the first bytes") {
      result = bench.in(CdcAcmFunctionBase::kGetLineCoding, 0u, 4u);
      CHECK(result.data == std::vector<uint8_t>(coding.begin(), coding.begin() + 4));
   }

   SUBCASE("a longer wLength gets 7 bytes") {
      result = bench.in(CdcAcmFunctionBase::kGetLineCoding, 0u, 64u);
      CHECK(result.data == coding);
   }

   SUBCASE("SET_LINE_CODING with a wrong length stalls and changes nothing") {
      CHECK_FALSE(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 0u, { 0x80u, 0x25u, 0u, 0u, 0u, 0u }));
      CHECK_FALSE(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 0u, { 0x80u, 0x25u, 0u, 0u, 0u, 0u, 8u, 0u }));
      CHECK(bench.cdc.lineCoding().dwDTERate == 921600u);
   }

   SUBCASE("the line coding stays after a bus reset") {
      bench.enumerate();
      CHECK(bench.cdc.lineCoding().dwDTERate == 921600u);
   }
}

TEST_CASE("[usb::CdcAcmFunction] unsupported requests stall") {
   Bench bench(false);
   bench.enumerate();
   // SEND_BREAK is not declared in the ACM functional descriptor.
   CHECK_FALSE(bench.out(kClassInterfaceOut, kSendBreak, 100u, 0u));
   // GET_LINE_CODING to the data interface.
   CHECK(bench.in(CdcAcmFunctionBase::kGetLineCoding, 1u, 7u).stalled);
   // SET_LINE_CODING as a device-to-host request.
   CHECK(bench.in(CdcAcmFunctionBase::kSetLineCoding, 0u, 7u).stalled);
   // A vendor request to the communication interface.
   CHECK_FALSE(bench.out(0x41u, CdcAcmFunctionBase::kSetControlLineState, 1u, 0u));
   CHECK_FALSE(bench.cdc.isTerminalOpen());
}

TEST_CASE("[usb::CdcAcmFunction] no data path before the configuration") {
   Bench bench(false);
   const std::vector<uint8_t> data = pattern(10u);
   CHECK_FALSE(bench.cdc.isConfigured());
   CHECK(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == 0u);
   CHECK(bench.cdc.writeAvailable() == 0u);
   CHECK_FALSE(bench.cdc.notifySerialState(CdcAcmFunctionBase::kSerialStateDcd));

   bench.enumerate();
   CHECK(bench.cdc.writeAvailable() == 256u);
   CHECK(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == 10u);
}

TEST_CASE("[usb::CdcAcmFunction] deconfiguration clears the queues") {
   Bench bench(false);
   bench.enumerate();
   REQUIRE(bench.sendOut(pattern(20u)));
   const std::vector<uint8_t> data = pattern(200u);
   REQUIRE(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == 200u);

   bench.enumerate();
   CHECK(bench.cdc.readAvailable() == 0u);
   CHECK(bench.cdc.isTxIdle());
   CHECK_FALSE(bench.takeIn(kDataIn).has_value());
   CHECK(bench.controller().isArmed(kDataOut));

   // The data path works again.
   REQUIRE(bench.cdc.write(art::span<const uint8_t>(data.data(), 3u)) == 3u);
   CHECK(bench.takeIn(kDataIn) == std::vector<uint8_t>{ 0u, 1u, 2u });
}

TEST_CASE("[usb::CdcAcmFunction] SET_INTERFACE on the data interface restarts the data path") {
   Bench bench(false);
   bench.enumerate();
   const std::vector<uint8_t> data = pattern(70u);
   REQUIRE(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == 70u);
   // The endpoints are reopened: the first packet, which the host did not read, is lost, the rest goes out.
   CHECK(bench.out(kStandardInterfaceOut, kSetInterface, 0u, 1u));
   CHECK(bench.controller().isArmed(kDataOut));
   CHECK(bench.takeIn(kDataIn) == pattern(6u, 64u));
   CHECK_FALSE(bench.takeIn(kDataIn).has_value());
   REQUIRE(bench.sendOut(pattern(5u)));
   CHECK(readAll(bench.cdc) == pattern(5u));
}

TEST_CASE("[usb::CdcAcmFunction] SERIAL_STATE notification") {
   Bench bench(false);
   bench.enumerate();
   CHECK(bench.cdc.notifySerialState(CdcAcmFunctionBase::kSerialStateDcd | CdcAcmFunctionBase::kSerialStateDsr));
   // A second notification waits until the first one is out.
   CHECK_FALSE(bench.cdc.notifySerialState(0u));

   // 10 bytes in packets of 8: the header, then the UART state bitmap.
   CHECK(bench.takeIn(kNotification) == std::vector<uint8_t>{ 0xA1u, 0x20u, 0u, 0u, 0u, 0u, 2u, 0u });
   CHECK_FALSE(bench.cdc.notifySerialState(0u));
   CHECK(bench.takeIn(kNotification) == std::vector<uint8_t>{ 0x03u, 0x00u });
   CHECK_FALSE(bench.takeIn(kNotification).has_value());

   CHECK(bench.cdc.notifySerialState(CdcAcmFunctionBase::kSerialStateOverrun));
   CHECK(bench.takeIn(kNotification) == std::vector<uint8_t>{ 0xA1u, 0x20u, 0u, 0u, 0u, 0u, 2u, 0u });
   CHECK(bench.takeIn(kNotification) == std::vector<uint8_t>{ 0x40u, 0x00u });
}
