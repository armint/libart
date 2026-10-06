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
constexpr uint8_t kStandardEndpointOut = 0x02u;
constexpr uint8_t kClearFeature = 1u;
constexpr uint8_t kSetFeature = 3u;
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

   /// SET_FEATURE(ENDPOINT_HALT).
   void halt(uint8_t endpointAddress) {
      CHECK(out(kStandardEndpointOut, kSetFeature, 0u, endpointAddress));
      poll();
   }

   /// CLEAR_FEATURE(ENDPOINT_HALT), as a host sends it after a stall, or when it opens the pipe.
   void clearHalt(uint8_t endpointAddress) {
      CHECK(out(kStandardEndpointOut, kClearFeature, 0u, endpointAddress));
      poll();
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
   // The queue takes 256 bytes. The packet on its way keeps its room until the host acknowledges it.
   const std::vector<uint8_t> data = pattern(400u);
   CHECK(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == 256u);
   CHECK(bench.cdc.writeAvailable() == 0u);
   CHECK(bench.cdc.write(art::span<const uint8_t>(data.data() + 256, 1u)) == 0u);
   std::vector<uint8_t> received = *bench.takeIn(kDataIn);
   CHECK(bench.cdc.writeAvailable() == 64u);
   CHECK(bench.cdc.write(art::span<const uint8_t>(data.data() + 256, 144u)) == 64u);
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
   // SEND_BREAK without an event handler.
   CHECK_FALSE(bench.out(kClassInterfaceOut, kSendBreak, 100u, 0u));
   // GET_LINE_CODING to the data interface.
   CHECK(bench.in(CdcAcmFunctionBase::kGetLineCoding, 1u, 7u).stalled);
   // SET_LINE_CODING as a device-to-host request.
   CHECK(bench.in(CdcAcmFunctionBase::kSetLineCoding, 0u, 7u).stalled);
   // A vendor request to the communication interface.
   CHECK_FALSE(bench.out(0x41u, CdcAcmFunctionBase::kSetControlLineState, 1u, 0u));
   CHECK_FALSE(bench.cdc.isTerminalOpen());
}

namespace {

/// Records the calls of the CDC-ACM event handler.
class RecordingEvents final : public CdcAcmEvents {
   public:
   struct ControlLineChange {
      uint16_t state;
      uint16_t previousState;
      /// isTerminalOpen() of the function during the call.
      bool terminalOpen;
      /// isConfigured() of the function during the call.
      bool configured;

      bool operator==(const ControlLineChange &other) const {
         return state == other.state && previousState == other.previousState && terminalOpen == other.terminalOpen &&
                configured == other.configured;
      }
   };

   explicit RecordingEvents(const CdcAcmFunctionBase &cdc)
      : cdc_(cdc) {
   }

   bool lineCodingRequested(const LineCoding &requested) override {
      lineCodings.push_back(requested);
      // The function stores the line coding only after the call.
      lineCodingDuringCall = cdc_.lineCoding();
      return acceptLineCoding;
   }

   void controlLineStateChanged(uint16_t state, uint16_t previousState) override {
      CHECK(cdc_.controlLineState() == state);
      controlLineChanges.push_back(ControlLineChange{ state, previousState, cdc_.isTerminalOpen(), cdc_.isConfigured() });
   }

   bool breakRequested(uint16_t durationMilliseconds) override {
      breaks.push_back(durationMilliseconds);
      return acceptBreak;
   }

   bool acceptLineCoding{ true };
   bool acceptBreak{ true };
   std::vector<LineCoding> lineCodings;
   LineCoding lineCodingDuringCall{};
   std::vector<ControlLineChange> controlLineChanges;
   std::vector<uint16_t> breaks;

   private:
   const CdcAcmFunctionBase &cdc_;
};

} // namespace

TEST_CASE("[usb::CdcAcmFunction] the event handler gets SET_LINE_CODING") {
   Bench bench(false);
   RecordingEvents events(bench.cdc);
   bench.cdc.setEventHandler(&events);
   bench.enumerate();

   // 9600 baud, 1 stop bit, odd parity, 7 data bits.
   const std::vector<uint8_t> coding{ 0x80u, 0x25u, 0x00u, 0x00u, 0u, 1u, 7u };
   CHECK(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 0u, coding));
   REQUIRE(events.lineCodings.size() == 1u);
   CHECK(events.lineCodings[0].dwDTERate == 9600u);
   CHECK(events.lineCodings[0].bCharFormat == 0u);
   CHECK(events.lineCodings[0].bParityType == 1u);
   CHECK(events.lineCodings[0].bDataBits == 7u);
   CHECK(events.lineCodingDuringCall == LineCoding{});
   CHECK(bench.cdc.lineCoding() == events.lineCodings[0]);

   SUBCASE("the same line coding again calls the handler again") {
      CHECK(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 0u, coding));
      CHECK(events.lineCodings.size() == 2u);
      CHECK(events.lineCodingDuringCall == bench.cdc.lineCoding());
   }

   SUBCASE("a refused line coding stalls and keeps the old one") {
      events.acceptLineCoding = false;
      CHECK_FALSE(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 0u, { 0x00u, 0x10u, 0x0Eu, 0x00u, 0u, 0u, 5u }));
      REQUIRE(events.lineCodings.size() == 2u);
      CHECK(events.lineCodings[1].dwDTERate == 921600u);
      CHECK(bench.cdc.lineCoding().dwDTERate == 9600u);
      CHECK(bench.in(CdcAcmFunctionBase::kGetLineCoding, 0u, 7u).data == coding);
   }

   SUBCASE("a malformed request does not reach the handler") {
      CHECK_FALSE(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 0u, { 0x80u, 0x25u, 0u, 0u, 0u, 0u }));
      CHECK_FALSE(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 1u, coding));
      CHECK(events.lineCodings.size() == 1u);
   }

   SUBCASE("without the handler the line coding is stored") {
      bench.cdc.setEventHandler(nullptr);
      CHECK(bench.outData(CdcAcmFunctionBase::kSetLineCoding, 0u, { 0x00u, 0x10u, 0x0Eu, 0x00u, 0u, 0u, 8u }));
      CHECK(events.lineCodings.size() == 1u);
      CHECK(bench.cdc.lineCoding().dwDTERate == 921600u);
   }
}

TEST_CASE("[usb::CdcAcmFunction] the event handler gets each change of the control lines") {
   using Change = RecordingEvents::ControlLineChange;
   constexpr uint16_t kDtr = CdcAcmFunctionBase::kControlLineDtr;
   constexpr uint16_t kRts = CdcAcmFunctionBase::kControlLineRts;
   Bench bench(false);
   RecordingEvents events(bench.cdc);
   bench.cdc.setEventHandler(&events);
   bench.enumerate();
   CHECK(events.controlLineChanges.empty());

   // A terminal opens the port (Linux sets DTR and RTS), sets the same state again, and drops RTS.
   CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, kDtr | kRts, 0u));
   CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, kDtr | kRts, 0u));
   CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, kDtr, 0u));
   CHECK(events.controlLineChanges ==
         std::vector<Change>{ Change{ kDtr | kRts, 0u, true, true }, Change{ kDtr, kDtr | kRts, true, true } });
   events.controlLineChanges.clear();

   SUBCASE("the terminal closes the port") {
      CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, 0u, 0u));
      CHECK(events.controlLineChanges == std::vector<Change>{ Change{ 0u, kDtr, false, true } });
   }

   SUBCASE("unknown bits of wValue are not a change") {
      CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, kDtr | 0x0100u, 0u));
      CHECK(events.controlLineChanges.empty());
   }

   SUBCASE("a bus reset clears the state, after the function is deconfigured") {
      bench.controller().busReset();
      CHECK(events.controlLineChanges == std::vector<Change>{ Change{ 0u, kDtr, false, false } });
      // A second reset changes nothing.
      bench.controller().busReset();
      CHECK(events.controlLineChanges.size() == 1u);
   }

   SUBCASE("SET_CONFIGURATION 0 clears the state") {
      CHECK(bench.out(kStandardDeviceOut, kSetConfiguration, 0u, 0u));
      CHECK(events.controlLineChanges == std::vector<Change>{ Change{ 0u, kDtr, false, false } });
   }

   SUBCASE("the request to the data interface stalls without a call") {
      CHECK_FALSE(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSetControlLineState, 0u, 1u));
      CHECK(events.controlLineChanges.empty());
   }
}

TEST_CASE("[usb::CdcAcmFunction] SEND_BREAK goes to the event handler") {
   Bench bench(false);
   RecordingEvents events(bench.cdc);
   bench.enumerate();

   SUBCASE("without the handler it stalls") {
      CHECK_FALSE(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSendBreak, 100u, 0u));
   }

   SUBCASE("the handler accepts it") {
      bench.cdc.setEventHandler(&events);
      CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSendBreak, 250u, 0u));
      CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSendBreak, 0xFFFFu, 0u));
      CHECK(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSendBreak, 0u, 0u));
      CHECK(events.breaks == std::vector<uint16_t>{ 250u, 0xFFFFu, 0u });
   }

   SUBCASE("the handler refuses it") {
      bench.cdc.setEventHandler(&events);
      events.acceptBreak = false;
      CHECK_FALSE(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSendBreak, 100u, 0u));
      CHECK(events.breaks == std::vector<uint16_t>{ 100u });
   }

   SUBCASE("a malformed request does not reach the handler") {
      bench.cdc.setEventHandler(&events);
      // To the data interface, and with a data stage.
      CHECK_FALSE(bench.out(kClassInterfaceOut, CdcAcmFunctionBase::kSendBreak, 100u, 1u));
      CHECK_FALSE(bench.outData(CdcAcmFunctionBase::kSendBreak, 0u, { 0u }));
      CHECK(events.breaks.empty());
   }
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
   // The endpoints are reopened: the first packet, which the host did not read, goes out again.
   CHECK(bench.out(kStandardInterfaceOut, kSetInterface, 0u, 1u));
   CHECK(bench.controller().isArmed(kDataOut));
   std::vector<uint8_t> received;
   CHECK(bench.readTransfer(received) == std::vector<size_t>{ 64u, 6u });
   CHECK(received == data);
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

namespace {

/// Halts and clears the endpoints in different states. The SUBCASEs are in a function, not in a loop, so that
/// doctest runs each of them once per event mode.
void checkEndpointHalt(bool queueEvents) {
   Bench bench(queueEvents);
   bench.enumerate();

   SUBCASE("halt of the IN endpoint with a packet on its way") {
      const std::vector<uint8_t> data = pattern(110u);
      REQUIRE(bench.cdc.write(art::span<const uint8_t>(data.data(), 100u)) == 100u);
      bench.halt(kDataIn);
      CHECK_FALSE(bench.takeIn(kDataIn).has_value());
      // Data written while the endpoint is halted waits.
      REQUIRE(bench.cdc.write(art::span<const uint8_t>(data.data() + 100, 10u)) == 10u);
      CHECK_FALSE(bench.takeIn(kDataIn).has_value());
      bench.clearHalt(kDataIn);
      std::vector<uint8_t> received;
      CHECK(bench.readTransfer(received) == std::vector<size_t>{ 64u, 46u });
      CHECK(received == data);
      CHECK_FALSE(bench.takeIn(kDataIn).has_value());
      CHECK(bench.cdc.isTxIdle());
   }

   SUBCASE("halt of the IN endpoint while the ZLP is on its way") {
      const std::vector<uint8_t> data = pattern(64u);
      REQUIRE(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == 64u);
      CHECK(bench.takeIn(kDataIn) == data);
      bench.halt(kDataIn);
      bench.clearHalt(kDataIn);
      CHECK(bench.takeIn(kDataIn) == std::vector<uint8_t>{});
      CHECK_FALSE(bench.takeIn(kDataIn).has_value());
      CHECK(bench.cdc.isTxIdle());
   }

   SUBCASE("CLEAR_FEATURE on the IN endpoint without a halt") {
      const std::vector<uint8_t> data = pattern(10u);
      REQUIRE(bench.cdc.write(art::span<const uint8_t>(data.data(), data.size())) == 10u);
      bench.clearHalt(kDataIn);
      CHECK(bench.takeIn(kDataIn) == data);
      CHECK_FALSE(bench.takeIn(kDataIn).has_value());
   }

   SUBCASE("halt of the OUT endpoint") {
      REQUIRE(bench.sendOut(pattern(10u)));
      bench.halt(kDataOut);
      CHECK_FALSE(bench.sendOut(pattern(10u, 10u)));
      bench.clearHalt(kDataOut);
      CHECK(bench.controller().isArmed(kDataOut));
      REQUIRE(bench.sendOut(pattern(10u, 10u)));
      CHECK(readAll(bench.cdc) == pattern(20u));
   }

   SUBCASE("halt of the OUT endpoint while the RX queue is full") {
      REQUIRE(bench.sendOut(pattern(64u)));
      REQUIRE(bench.sendOut(pattern(64u, 64u)));
      bench.halt(kDataOut);
      bench.clearHalt(kDataOut);
      // Still no room for a full packet.
      CHECK_FALSE(bench.controller().isArmed(kDataOut));
      CHECK(readAll(bench.cdc) == pattern(128u));
      REQUIRE(bench.sendOut(pattern(64u, 128u)));
      CHECK(readAll(bench.cdc) == pattern(64u, 128u));
   }

   SUBCASE("CLEAR_FEATURE on the OUT endpoint without a halt") {
      bench.clearHalt(kDataOut);
      REQUIRE(bench.sendOut(pattern(5u)));
      CHECK(readAll(bench.cdc) == pattern(5u));
   }

   SUBCASE("halt of the notification endpoint drops the notification") {
      CHECK(bench.cdc.notifySerialState(CdcAcmFunctionBase::kSerialStateDcd));
      bench.halt(kNotification);
      bench.clearHalt(kNotification);
      CHECK_FALSE(bench.takeIn(kNotification).has_value());
      CHECK(bench.cdc.notifySerialState(CdcAcmFunctionBase::kSerialStateDcd));
      CHECK(bench.takeIn(kNotification).has_value());
   }

   // The loopback works after the halt.
   REQUIRE(bench.sendOut(pattern(3u, 200u)));
   const std::vector<uint8_t> received = readAll(bench.cdc);
   CHECK(bench.cdc.write(art::span<const uint8_t>(received.data(), received.size())) == 3u);
   CHECK(bench.takeIn(kDataIn) == pattern(3u, 200u));
}

} // namespace

TEST_CASE("[usb::CdcAcmFunction] the data path survives an endpoint halt (events at once)") {
   checkEndpointHalt(false);
}

TEST_CASE("[usb::CdcAcmFunction] the data path survives an endpoint halt (queued events)") {
   checkEndpointHalt(true);
}
