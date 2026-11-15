#include "usb/HidFunction.h"

#include "usb/Device.h"
#include <algorithm>

namespace art::usb {

HidFunction::HidFunction(uint8_t interfaceNumber, const VendorBulkEndpoints &endpoints, art::span<const uint8_t> reportDescriptor)
   : VendorBulkFunction(interfaceNumber, endpoints)
   , reportDescriptor_(reportDescriptor)
   , hidDescriptor_(descriptor::hidDescriptor(static_cast<uint16_t>(reportDescriptor.size()))) {
   if(reportDescriptor.size() > Device::kControlBufferSize) {
      // GET_DESCRIPTOR would send a truncated report descriptor (see "Limits").
      __builtin_trap();
   }
}

bool HidFunction::sendReport(art::span<const uint8_t> report) {
   if(report.size() != packetSize()) {
      return false;
   }
   // A full packet ends an interrupt transfer: no ZLP.
   return sendInTransfer(report, ZeroLengthPacket::Never);
}

std::optional<uint16_t> HidFunction::controlRequest(const SetupPacket &setup, art::span<uint8_t> data) {
   const bool deviceToHost = setup.direction() == RequestDirection::DeviceToHost;
   const auto valueHigh = static_cast<uint8_t>(setup.wValue >> 8u);
   const auto valueLow = static_cast<uint8_t>(setup.wValue & 0xFFu);

   if(setup.type() == RequestType::Standard) {
      if(setup.bRequest != static_cast<uint8_t>(StandardRequest::GetDescriptor) || !deviceToHost || valueLow != 0u) {
         return std::nullopt;
      }
      art::span<const uint8_t> reply;
      if(valueHigh == hid::kDescriptorTypeHid) {
         reply = art::span<const uint8_t>(hidDescriptor_.data(), hidDescriptor_.size());
      } else if(valueHigh == hid::kDescriptorTypeReport) {
         reply = reportDescriptor_;
      } else {
         return std::nullopt;
      }
      const size_t count = std::min(reply.size(), data.size());
      std::copy_n(reply.begin(), count, data.begin());
      return static_cast<uint16_t>(count);
   }

   if(setup.type() != RequestType::Class || setup.recipient() != RequestRecipient::Interface) {
      return std::nullopt;
   }
   switch(setup.bRequest) {
   case hid::kGetReport: {
      // Report ID 0: the function has no report IDs.
      if(!deviceToHost || valueHigh != hid::kReportTypeInput || valueLow != 0u) {
         return std::nullopt;
      }
      const size_t count = std::min<size_t>(packetSize(), data.size());
      std::fill_n(data.begin(), count, uint8_t{ 0u });
      return static_cast<uint16_t>(count);
   }
   case hid::kSetIdle:
      // The low byte selects the report ID: 0 means all reports.
      if(deviceToHost || setup.wLength != 0u || valueLow != 0u) {
         return std::nullopt;
      }
      idleRate_ = valueHigh;
      return 0u;
   case hid::kGetIdle:
      if(!deviceToHost || setup.wValue != 0u || data.empty()) {
         return std::nullopt;
      }
      data[0] = idleRate_;
      return 1u;
   default:
      return std::nullopt;
   }
}

} // namespace art::usb
