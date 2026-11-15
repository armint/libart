#include "FakeDeviceController.h"
#include "usb/DescriptorBuilder.h"
#include "usb/Device.h"
#include "usb/HidFunction.h"
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <optional>
#include <vector>

// usb::HidFunction through usb::Device and the fake controller: one HID interface with interrupt IN 0x81 and OUT
// 0x01, 64-byte reports at both speeds (bInterval 1: 1 ms at full speed, one 125 us microframe at high speed).

using namespace art::usb;
using namespace art::usb::descriptor;
using art::usb::test::FakeDeviceController;
using art::usb::test::setupPacket;

namespace {

constexpr uint8_t kStandardDeviceOut = 0x00u;
constexpr uint8_t kStandardInterfaceIn = 0x81u;
constexpr uint8_t kClassInterfaceOut = 0x21u;
constexpr uint8_t kClassInterfaceIn = 0xA1u;
constexpr uint8_t kGetDescriptor = 6u;
constexpr uint8_t kSetAddress = 5u;
constexpr uint8_t kSetConfiguration = 9u;

constexpr uint8_t kHidIn = 0x81u;
constexpr uint8_t kHidOut = 0x01u;
constexpr uint16_t kReportSize = 64u;

constexpr auto kReportDescriptor = vendorReportDescriptor<kReportSize>();

constexpr DeviceFields makeDeviceFields() {
   DeviceFields fields{};
   fields.bcdUSB = 0x0200u;
   fields.idVendor = 0x1209u;
   fields.idProduct = 0x0001u;
   return fields;
}

constexpr auto kDevice = device(makeDeviceFields());
constexpr auto kQualifier = deviceQualifier(makeDeviceFields());

constexpr auto makeConfiguration() {
   return configuration(ConfigurationFields{},
                        interfaceDescriptor(InterfaceFields{ 0u, 0u, hid::kInterfaceClass, 0u, 0u, 0u },
                                            hidDescriptor(static_cast<uint16_t>(kReportDescriptor.size())),
                                            endpoint(kHidIn, EndpointType::Interrupt, kReportSize, 1u),
                                            endpoint(kHidOut, EndpointType::Interrupt, kReportSize, 1u)));
}

constexpr auto kConfiguration = makeConfiguration();

/// Records the output reports.
class RecordingHid final : public HidFunction {
   public:
   RecordingHid()
      : HidFunction(0u, VendorBulkEndpoints{ kHidOut, kHidIn, kReportSize }, asSpan(kReportDescriptor)) {
   }

   std::vector<std::vector<uint8_t>> reports;
   int reportsSent{ 0 };

   protected:
   bool dataReceived(art::span<const uint8_t> packet) override {
      reports.emplace_back(packet.begin(), packet.end());
      return true;
   }

   void transferSent() override {
      ++reportsSent;
   }
};

class Bench {
   public:
   explicit Bench(bool highSpeed)
      : descriptors_{ asSpan(kDevice),
                      asSpan(kConfiguration),
                      {},
                      {},
                      highSpeed ? asSpan(kConfiguration) : art::span<const uint8_t>(),
                      highSpeed ? asSpan(kQualifier) : art::span<const uint8_t>() }
      , device_(controller_, descriptors_) {
      controller_.setHighSpeed(highSpeed);
      REQUIRE(device_.addFunction(hid));
      device_.start();
      controller_.busReset();
      CHECK(out(kStandardDeviceOut, kSetAddress, 5u, 0u));
      CHECK(out(kStandardDeviceOut, kSetConfiguration, 1u, 0u));
      REQUIRE(hid.isConfigured());
   }

   bool out(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index) {
      bool ok = false;
      const FakeDeviceController::ControlResult result =
      controller_.controlOut(setupPacket(requestType, request, value, index, 0u), art::span<const uint8_t>(), &ok);
      CHECK(ok);
      return !result.stalled;
   }

   FakeDeviceController::ControlResult in(uint8_t requestType, uint8_t request, uint16_t value, uint16_t length) {
      bool ok = false;
      FakeDeviceController::ControlResult result =
      controller_.controlIn(setupPacket(requestType, request, value, 0u, length), &ok);
      CHECK(ok);
      return result;
   }

   FakeDeviceController &controller() {
      return controller_;
   }

   RecordingHid hid;

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

} // namespace

TEST_CASE("[usb::hid] vendorReportDescriptor() encodes the report count in one or two bytes") {
   constexpr std::array<uint8_t, 27> kExpected64{ 0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x15, 0x00,
                                                  0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x09, 0x01,
                                                  0x81, 0x02, 0x95, 0x40, 0x09, 0x01, 0x91, 0x02, 0xC0 };
   CHECK(vendorReportDescriptor<64u>() == kExpected64);

   constexpr std::array<uint8_t, 29> kExpected512{ 0x06, 0x01, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x15, 0x00, 0x26,
                                                   0xFF, 0x00, 0x75, 0x08, 0x96, 0x00, 0x02, 0x09, 0x01, 0x81,
                                                   0x02, 0x96, 0x00, 0x02, 0x09, 0x01, 0x91, 0x02, 0xC0 };
   CHECK(vendorReportDescriptor<512u>(0xFF01u) == kExpected512);
}

TEST_CASE("[usb::hid] hidDescriptor() points to one report descriptor") {
   constexpr std::array<uint8_t, 9> kExpected{ 9u, 0x21u, 0x11u, 0x01u, 0u, 1u, 0x22u, 27u, 0u };
   CHECK(hidDescriptor(27u) == kExpected);
   // The configuration builder accepts the HID descriptor between the interface and the endpoints.
   CHECK(kConfiguration[4] == 1u);
   CHECK(kConfiguration[9u + 4u] == 2u);
}

TEST_CASE("[usb::HidFunction] GET_DESCRIPTOR returns the HID and the report descriptor") {
   for(const bool highSpeed : { false, true }) {
      CAPTURE(highSpeed);
      Bench bench(highSpeed);
      const auto hidResult = bench.in(kStandardInterfaceIn, kGetDescriptor, 0x2100u, 9u);
      CHECK_FALSE(hidResult.stalled);
      CHECK(hidResult.data == std::vector<uint8_t>(kConfiguration.begin() + 18, kConfiguration.begin() + 27));

      // The host asks for more than the descriptor has: the short reply ends the data stage.
      const auto reportResult = bench.in(kStandardInterfaceIn, kGetDescriptor, 0x2200u, 255u);
      CHECK_FALSE(reportResult.stalled);
      CHECK(reportResult.data == std::vector<uint8_t>(kReportDescriptor.begin(), kReportDescriptor.end()));

      // A host that reads only the start.
      const auto partResult = bench.in(kStandardInterfaceIn, kGetDescriptor, 0x2200u, 4u);
      CHECK(partResult.data == std::vector<uint8_t>(kReportDescriptor.begin(), kReportDescriptor.begin() + 4));

      // A physical descriptor (0x23) and a descriptor index other than 0 do not exist.
      CHECK(bench.in(kStandardInterfaceIn, kGetDescriptor, 0x2300u, 9u).stalled);
      CHECK(bench.in(kStandardInterfaceIn, kGetDescriptor, 0x2201u, 9u).stalled);
   }
}

TEST_CASE("[usb::HidFunction] class requests") {
   Bench bench(false);
   SUBCASE("GET_REPORT returns an input report of zeros") {
      const auto result = bench.in(kClassInterfaceIn, hid::kGetReport, 0x0100u, kReportSize);
      CHECK_FALSE(result.stalled);
      CHECK(result.data == std::vector<uint8_t>(kReportSize, 0u));
      // Feature and output reports, and report IDs, do not exist.
      CHECK(bench.in(kClassInterfaceIn, hid::kGetReport, 0x0300u, kReportSize).stalled);
      CHECK(bench.in(kClassInterfaceIn, hid::kGetReport, 0x0101u, kReportSize).stalled);
   }
   SUBCASE("SET_IDLE is accepted, GET_IDLE stalls") {
      CHECK(bench.out(kClassInterfaceOut, hid::kSetIdle, 0x7D00u, 0u));
      // The function has no report IDs.
      CHECK_FALSE(bench.out(kClassInterfaceOut, hid::kSetIdle, 0x0001u, 0u));
      CHECK(bench.in(kClassInterfaceIn, hid::kGetIdle, 0u, 1u).stalled);
   }
   SUBCASE("the protocol requests and SET_REPORT stall") {
      CHECK(bench.in(kClassInterfaceIn, hid::kGetProtocol, 0u, 1u).stalled);
      CHECK_FALSE(bench.out(kClassInterfaceOut, hid::kSetProtocol, 0u, 0u));
      CHECK_FALSE(bench.out(kClassInterfaceOut, hid::kSetReport, 0x0200u, 0u));
   }
}

TEST_CASE("[usb::HidFunction] reports go through the interrupt endpoints at both speeds") {
   for(const bool highSpeed : { false, true }) {
      CAPTURE(highSpeed);
      Bench bench(highSpeed);
      const std::optional<EndpointConfig> inEndpoint = bench.controller().endpoint(kHidIn);
      REQUIRE(inEndpoint.has_value());
      CHECK(inEndpoint->type == EndpointType::Interrupt);
      CHECK(bench.hid.packetSize() == kReportSize);

      const std::vector<uint8_t> request = pattern(kReportSize);
      REQUIRE(bench.controller().sendOut(kHidOut, art::span<const uint8_t>(request.data(), request.size())));
      CHECK(bench.hid.reports == std::vector<std::vector<uint8_t>>{ request });

      // A report of the wrong size is refused.
      const std::vector<uint8_t> shortReport = pattern(10u);
      CHECK_FALSE(bench.hid.sendReport(art::span<const uint8_t>(shortReport.data(), shortReport.size())));

      // One full report, and no ZLP after it.
      const std::vector<uint8_t> response = pattern(kReportSize, 0x80u);
      REQUIRE(bench.hid.sendReport(art::span<const uint8_t>(response.data(), response.size())));
      CHECK(bench.controller().takeIn(kHidIn) == std::optional<std::vector<uint8_t>>(response));
      CHECK(bench.hid.reportsSent == 1);
      CHECK_FALSE(bench.hid.isInTransferBusy());
      CHECK_FALSE(bench.controller().takeIn(kHidIn).has_value());
   }
}
