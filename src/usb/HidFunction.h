#pragma once

#include "usb/DescriptorBuilder.h"
#include "usb/VendorBulkFunction.h"
#include "utils/compat/span.h"
#include <array>
#include <cstdint>
#include <optional>

/**
 * @file
 * @brief The HID class (HID 1.11): descriptor builders and HidFunction, a function with one interface and an
 * interrupt IN and OUT endpoint that exchanges fixed-size reports without report IDs.
 */

namespace art::usb {

/// Codes of the HID 1.11 specification.
namespace hid {
/// bInterfaceClass of a HID interface.
constexpr uint8_t kInterfaceClass = 0x03u;
/// bDescriptorType of the HID descriptor (HID 1.11 section 7.1).
constexpr uint8_t kDescriptorTypeHid = 0x21u;
/// bDescriptorType of the report descriptor.
constexpr uint8_t kDescriptorTypeReport = 0x22u;
/// Class-specific requests (HID 1.11 section 7.2).
constexpr uint8_t kGetReport = 0x01u;
constexpr uint8_t kGetIdle = 0x02u;
constexpr uint8_t kGetProtocol = 0x03u;
constexpr uint8_t kSetReport = 0x09u;
constexpr uint8_t kSetIdle = 0x0Au;
constexpr uint8_t kSetProtocol = 0x0Bu;
/// The report type in the high byte of wValue of GET_REPORT and SET_REPORT.
constexpr uint8_t kReportTypeInput = 0x01u;
constexpr uint8_t kReportTypeOutput = 0x02u;
constexpr uint8_t kReportTypeFeature = 0x03u;
} // namespace hid

namespace descriptor {

/**
 * @brief The 9-byte HID descriptor (HID 1.11 section 6.2.1) with one report descriptor: HID 1.11, not localized.
 *
 * Put it into interfaceDescriptor() before the endpoint descriptors. HidFunction answers GET_DESCRIPTOR(HID) with
 * the same bytes, so the descriptor has no other fields to set.
 *
 * @param reportDescriptorLength The size of the report descriptor.
 */
constexpr std::array<uint8_t, 9> hidDescriptor(uint16_t reportDescriptorLength) {
   return {
      9u, hid::kDescriptorTypeHid, 0x11u, 0x01u, 0u, 1u, hid::kDescriptorTypeReport, detail::lowByte(reportDescriptorLength), detail::highByte(reportDescriptorLength)
   };
}

/**
 * @brief A report descriptor with one vendor-defined input report and one vendor-defined output report of
 * `ReportSize` bytes each, without report IDs. This is the report format of CMSIS-DAP v1 and of the Microchip
 * EDBG tools.
 *
 * @param usagePage The vendor-defined usage page (0xFF00 to 0xFFFF).
 */
template <uint16_t ReportSize>
constexpr auto vendorReportDescriptor(uint16_t usagePage = 0xFF00u) {
   static_assert(ReportSize >= 1u, "a report has at least one byte");
   // The Report Count item takes one data byte up to 255 and two bytes above.
   constexpr bool kLongCount = ReportSize > 0xFFu;
   constexpr size_t kCountItemSize = kLongCount ? 3u : 2u;
   constexpr uint8_t kCountItem = kLongCount ? 0x96u : 0x95u;
   std::array<uint8_t, 15u + 2u * (kCountItemSize + 4u)> result{};
   size_t offset = 0u;
   auto add = [&result, &offset](uint8_t value) { result[offset++] = value; };
   auto addCount = [&]() {
      add(kCountItem);
      add(detail::lowByte(ReportSize));
      if(kLongCount) {
         add(detail::highByte(ReportSize));
      }
   };
   add(0x06u); // Usage Page (16 bit)
   add(detail::lowByte(usagePage));
   add(detail::highByte(usagePage));
   add(0x09u); // Usage 1
   add(0x01u);
   add(0xA1u); // Collection (Application)
   add(0x01u);
   add(0x15u); // Logical Minimum 0
   add(0x00u);
   add(0x26u); // Logical Maximum 255
   add(0xFFu);
   add(0x00u);
   add(0x75u); // Report Size 8 bits
   add(0x08u);
   addCount();
   add(0x09u); // Usage 1
   add(0x01u);
   add(0x81u); // Input (Data, Variable, Absolute)
   add(0x02u);
   addCount();
   add(0x09u); // Usage 1
   add(0x01u);
   add(0x91u); // Output (Data, Variable, Absolute)
   add(0x02u);
   add(0xC0u); // End Collection
   return result;
}

} // namespace descriptor

/**
 * @brief A HID function with one interface, one interrupt OUT and one interrupt IN endpoint, for fixed-size reports
 * without report IDs (for example CMSIS-DAP v1 or a vendor-defined protocol).
 *
 * The data path is the one of VendorBulkFunction: it works the same on interrupt endpoints. One OUT packet is one
 * output report, and the report size is the wMaxPacketSize of the endpoints (packetSize()). A subclass gets each
 * output report in dataReceived() and sends an input report with sendReport(). Hold and resumeOut(), endpoint halt
 * and configuration changes work as described in VendorBulkFunction.
 *
 * The function answers the HID requests itself:
 * - GET_DESCRIPTOR of the HID descriptor and of the report descriptor;
 * - GET_REPORT (input): a report of zeros, because the reports go through the interrupt endpoints;
 * - SET_IDLE: accepted and ignored, because the function sends input reports only on request;
 * - GET_IDLE, SET_REPORT, GET_PROTOCOL and SET_PROTOCOL stall (optional for a device that is not a boot device).
 * A subclass that overrides controlRequest() calls HidFunction::controlRequest() for the requests it does not know.
 *
 * Build the descriptors with descriptor::hidDescriptor() and descriptor::vendorReportDescriptor():
 * @code
 * constexpr auto kReport = descriptor::vendorReportDescriptor<64>();
 * interfaceDescriptor(InterfaceFields{ 0, 0, hid::kInterfaceClass, 0, 0, 0 },
 *    hidDescriptor(kReport.size()),
 *    endpoint(0x81, EndpointType::Interrupt, 64, 1),
 *    endpoint(0x01, EndpointType::Interrupt, 64, 1))
 * @endcode
 *
 * @par Limits
 * - The report descriptor is sent from the control buffer of usb::Device, so it must not be larger than
 *   Device::kControlBufferSize (64 bytes); the constructor traps otherwise. vendorReportDescriptor() is 27 or 29 bytes.
 * - One report descriptor serves both speeds. With high-speed descriptors, give the endpoints the same wMaxPacketSize
 *   at both speeds (a different report size per speed is not supported).
 */
class HidFunction : public VendorBulkFunction {
   public:
   /**
    * @brief Sends one input report.
    *
    * @param report Exactly packetSize() bytes. It is not copied: keep it valid and unchanged until transferSent() or
    * dataPathClosed().
    * @return false if the size is wrong, or for the reasons of VendorBulkFunction::sendInTransfer().
    */
   bool sendReport(art::span<const uint8_t> report);

   std::optional<uint16_t> controlRequest(const SetupPacket &setup, art::span<uint8_t> data) override;

   protected:
   /**
    * @param interfaceNumber The bInterfaceNumber of the interface.
    * @param endpoints The interrupt endpoints of the interface.
    * @param reportDescriptor The report descriptor (for example in flash). It must stay valid as long as the function.
    */
   HidFunction(uint8_t interfaceNumber, const VendorBulkEndpoints &endpoints, art::span<const uint8_t> reportDescriptor);

   private:
   art::span<const uint8_t> reportDescriptor_;
};

} // namespace art::usb
