#include "usb/VendorBulkFunction.h"

#include "usb/Device.h"
#include <algorithm>

namespace art::usb {

VendorBulkFunction::VendorBulkFunction(uint8_t interfaceNumber, const VendorBulkEndpoints &endpoints)
   : Function(interfaceNumber, 1u)
   , endpoints_(endpoints) {
   // A packet size of 0 would never send the data of a transfer.
   endpoints_.maxPacketSize = std::max<uint16_t>(endpoints_.maxPacketSize, 1u);
}

bool VendorBulkFunction::sendInTransfer(art::span<const uint8_t> data, ZeroLengthPacket zeroLengthPacket) {
   if(!configured_ || inTransferBusy_) {
      return false;
   }
   inData_ = data;
   inSent_ = 0u;
   inPacketSize_ = 0u;
   // An empty transfer is a ZLP. Otherwise a full last packet does not end a bulk transfer for the host.
   inZlpPending_ = data.empty() || (zeroLengthPacket == ZeroLengthPacket::Auto && data.size() % endpoints_.maxPacketSize == 0u);
   inTransferBusy_ = true;
   sendNextInPacket();
   return true;
}

void VendorBulkFunction::resumeOut() {
   if(!outHeld_) {
      return;
   }
   outHeld_ = false;
   armOut();
}

void VendorBulkFunction::configured() {
   resetDataPath();
   configured_ = true;
   armOut();
   dataPathOpened();
}

void VendorBulkFunction::deconfigured() {
   configured_ = false;
   resetDataPath();
   dataPathClosed();
}

void VendorBulkFunction::alternateSettingChanged(uint8_t interfaceNumber, uint8_t alternateSetting) {
   static_cast<void>(alternateSetting);
   // SET_INTERFACE closed and reopened the endpoints: the controller dropped the packet on its way and the OUT arm.
   if(interfaceNumber == firstInterface()) {
      restartOut();
      restartIn();
   }
}

void VendorBulkFunction::endpointHaltCleared(uint8_t endpointAddress) {
   // The controller dropped the transfer of the endpoint (DeviceController, "Endpoint halt"). The host can clear the
   // halt of an endpoint that is not halted, too.
   if(endpointAddress == endpoints_.out) {
      restartOut();
   } else if(endpointAddress == endpoints_.in) {
      restartIn();
   }
}

void VendorBulkFunction::outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) {
   if(endpointAddress != endpoints_.out || !configured_) {
      return;
   }
   outArmed_ = false;
   if(!dataReceived(packet)) {
      // A resumeOut() inside the hook did nothing (the endpoint was not held yet), so the hold starts here.
      outHeld_ = true;
      return;
   }
   armOut();
}

void VendorBulkFunction::inPacketSent(uint8_t endpointAddress) {
   if(endpointAddress != endpoints_.in || !inPacketBusy_) {
      return;
   }
   inPacketBusy_ = false;
   if(inPacketSize_ == 0u) {
      inZlpPending_ = false;
   }
   inSent_ += inPacketSize_;
   inPacketSize_ = 0u;
   if(inSent_ < inData_.size() || inZlpPending_) {
      sendNextInPacket();
      return;
   }
   // Clear the transfer before the hook, so that it can start the next one.
   inTransferBusy_ = false;
   inData_ = art::span<const uint8_t>();
   transferSent();
}

void VendorBulkFunction::armOut() {
   if(!configured_ || outArmed_ || outHeld_ || device() == nullptr || device()->isEndpointHalted(endpoints_.out)) {
      return;
   }
   device()->armOutTransfer(endpoints_.out);
   outArmed_ = true;
}

void VendorBulkFunction::sendNextInPacket() {
   if(!configured_ || !inTransferBusy_ || inPacketBusy_ || device() == nullptr || device()->isEndpointHalted(endpoints_.in)) {
      return;
   }
   const size_t count = std::min<size_t>(inData_.size() - inSent_, endpoints_.maxPacketSize);
   if(count == 0u && !inZlpPending_) {
      return;
   }
   // The controller copies the packet. inSent_ moves only when the host acknowledges it, so a dropped packet goes
   // out again.
   if(!device()->startInTransfer(endpoints_.in, inData_.subspan(inSent_, count))) {
      return;
   }
   inPacketSize_ = count;
   inPacketBusy_ = true;
}

void VendorBulkFunction::restartOut() {
   outArmed_ = false;
   armOut();
}

void VendorBulkFunction::restartIn() {
   inPacketBusy_ = false;
   inPacketSize_ = 0u;
   sendNextInPacket();
}

void VendorBulkFunction::resetDataPath() {
   outArmed_ = false;
   outHeld_ = false;
   inData_ = art::span<const uint8_t>();
   inSent_ = 0u;
   inPacketSize_ = 0u;
   inTransferBusy_ = false;
   inPacketBusy_ = false;
   inZlpPending_ = false;
}

} // namespace art::usb
