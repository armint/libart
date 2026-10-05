#include "usb/CdcAcmFunction.h"

#include "usb/Device.h"
#include <algorithm>

namespace art::usb {

CdcAcmFunctionBase::CdcAcmFunctionBase(uint8_t controlInterface,
                                       const CdcAcmEndpoints &endpoints,
                                       etl::icircular_buffer<uint8_t> &rxQueue,
                                       etl::icircular_buffer<uint8_t> &txQueue)
   : Function(controlInterface, 2u)
   , endpoints_(endpoints)
   , rxQueue_(rxQueue)
   , txQueue_(txQueue) {
}

size_t CdcAcmFunctionBase::read(art::span<uint8_t> buffer) {
   const size_t count = std::min(buffer.size(), rxQueue_.size());
   for(size_t index = 0u; index < count; ++index) {
      buffer[index] = rxQueue_.front();
      rxQueue_.pop();
   }
   armOutIfRoom();
   return count;
}

size_t CdcAcmFunctionBase::write(art::span<const uint8_t> data) {
   if(!configured_) {
      return 0u;
   }
   // etl::circular_buffer::push() overwrites the oldest byte when the queue is full, so push only what fits.
   const size_t count = std::min(data.size(), txQueue_.available());
   for(size_t index = 0u; index < count; ++index) {
      txQueue_.push(data[index]);
   }
   sendNextInPacket();
   return count;
}

bool CdcAcmFunctionBase::notifySerialState(uint16_t serialState) {
   if(!configured_ || notificationBusy_ || notificationSent_ < notification_.size()) {
      return false;
   }
   // A class-specific notification is a setup-like header (PSTN 1.20 section 6.5.4) and the data.
   notification_ = { 0xA1u, // device-to-host, class, interface
                     kSerialStateNotification,
                     0u,
                     0u,
                     firstInterface(),
                     0u,
                     2u,
                     0u,
                     static_cast<uint8_t>(serialState & 0xFFu),
                     static_cast<uint8_t>(serialState >> 8u) };
   notificationSent_ = 0u;
   sendNextNotificationPacket();
   return true;
}

std::optional<uint16_t> CdcAcmFunctionBase::controlRequest(const SetupPacket &setup, art::span<uint8_t> data) {
   // The ACM requests go to the communication interface. Requests to the data interface or its endpoints stall.
   if(setup.type() != RequestType::Class || setup.recipient() != RequestRecipient::Interface || setup.wIndex != firstInterface()) {
      return std::nullopt;
   }
   switch(setup.bRequest) {
   case kSetLineCoding:
      if(setup.direction() != RequestDirection::HostToDevice || data.size() != LineCoding::kSize) {
         return std::nullopt;
      }
      lineCoding_.dwDTERate = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8u) |
                              (static_cast<uint32_t>(data[2]) << 16u) | (static_cast<uint32_t>(data[3]) << 24u);
      lineCoding_.bCharFormat = data[4];
      lineCoding_.bParityType = data[5];
      lineCoding_.bDataBits = data[6];
      return 0u;
   case kGetLineCoding: {
      if(setup.direction() != RequestDirection::DeviceToHost) {
         return std::nullopt;
      }
      const std::array<uint8_t, LineCoding::kSize> reply{ static_cast<uint8_t>(lineCoding_.dwDTERate & 0xFFu),
                                                          static_cast<uint8_t>((lineCoding_.dwDTERate >> 8u) & 0xFFu),
                                                          static_cast<uint8_t>((lineCoding_.dwDTERate >> 16u) & 0xFFu),
                                                          static_cast<uint8_t>((lineCoding_.dwDTERate >> 24u) & 0xFFu),
                                                          lineCoding_.bCharFormat,
                                                          lineCoding_.bParityType,
                                                          lineCoding_.bDataBits };
      const size_t count = std::min(reply.size(), data.size());
      std::copy_n(reply.begin(), count, data.begin());
      return static_cast<uint16_t>(count);
   }
   case kSetControlLineState:
      if(setup.direction() != RequestDirection::HostToDevice || setup.wLength != 0u) {
         return std::nullopt;
      }
      controlLineState_ = static_cast<uint16_t>(setup.wValue & (kControlLineDtr | kControlLineRts));
      return 0u;
   default:
      return std::nullopt;
   }
}

void CdcAcmFunctionBase::configured() {
   configured_ = true;
   rxQueue_.clear();
   txQueue_.clear();
   resetDataEndpoints();
   notificationSent_ = notification_.size();
   notificationBusy_ = false;
   armOutIfRoom();
}

void CdcAcmFunctionBase::deconfigured() {
   configured_ = false;
   controlLineState_ = 0u;
   rxQueue_.clear();
   txQueue_.clear();
   resetDataEndpoints();
   notificationSent_ = notification_.size();
   notificationBusy_ = false;
}

void CdcAcmFunctionBase::alternateSettingChanged(uint8_t interfaceNumber, uint8_t alternateSetting) {
   static_cast<void>(alternateSetting);
   // SET_INTERFACE closed and reopened the endpoints of the interface: a packet on its way is lost, and the OUT
   // endpoint is no longer armed. The data in the queues stays.
   if(interfaceNumber == dataInterface()) {
      resetDataEndpoints();
      armOutIfRoom();
      sendNextInPacket();
   } else if(interfaceNumber == firstInterface()) {
      notificationSent_ = notification_.size();
      notificationBusy_ = false;
   }
}

void CdcAcmFunctionBase::outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) {
   if(endpointAddress != endpoints_.dataOut) {
      return;
   }
   outArmed_ = false;
   // The endpoint was armed only with room for a full packet, so everything fits. The check protects the queue
   // against a controller that delivers a packet larger than kDataPacketSize.
   const size_t count = std::min(packet.size(), rxQueue_.available());
   for(size_t index = 0u; index < count; ++index) {
      rxQueue_.push(packet[index]);
   }
   armOutIfRoom();
}

void CdcAcmFunctionBase::inPacketSent(uint8_t endpointAddress) {
   if(endpointAddress == endpoints_.dataIn) {
      txBusy_ = false;
      sendNextInPacket();
   } else if(endpointAddress == endpoints_.notification) {
      notificationBusy_ = false;
      sendNextNotificationPacket();
   }
}

void CdcAcmFunctionBase::armOutIfRoom() {
   if(!configured_ || outArmed_ || rxQueue_.available() < kDataPacketSize || device() == nullptr) {
      return;
   }
   device()->armOutTransfer(endpoints_.dataOut);
   outArmed_ = true;
}

void CdcAcmFunctionBase::sendNextInPacket() {
   if(!configured_ || txBusy_ || device() == nullptr) {
      return;
   }
   const size_t count = std::min<size_t>(txQueue_.size(), kDataPacketSize);
   if(count == 0u && !zlpPending_) {
      return;
   }
   // The controller copies the packet, so it can be built on the stack. The bytes leave the queue only when the
   // controller took them.
   std::array<uint8_t, kDataPacketSize> packet{};
   for(size_t index = 0u; index < count; ++index) {
      packet[index] = txQueue_[index];
   }
   if(!device()->startInTransfer(endpoints_.dataIn, art::span<const uint8_t>(packet.data(), count))) {
      return;
   }
   txQueue_.pop(count);
   txBusy_ = true;
   // A full packet does not end a bulk transfer for the host; a short packet or a ZLP does.
   zlpPending_ = count == kDataPacketSize;
}

void CdcAcmFunctionBase::sendNextNotificationPacket() {
   if(notificationBusy_ || notificationSent_ >= notification_.size() || device() == nullptr) {
      return;
   }
   const size_t count =
   std::min<size_t>(notification_.size() - notificationSent_, std::max<uint16_t>(endpoints_.notificationMaxPacketSize, 1u));
   if(!device()->startInTransfer(endpoints_.notification, art::span<const uint8_t>(notification_.data() + notificationSent_, count))) {
      // The endpoint is not open: drop the notification, so that the next one can go.
      notificationSent_ = notification_.size();
      return;
   }
   notificationSent_ += count;
   notificationBusy_ = true;
}

void CdcAcmFunctionBase::resetDataEndpoints() {
   outArmed_ = false;
   txBusy_ = false;
   zlpPending_ = false;
}

} // namespace art::usb
