#include "usb/Device.h"

#include <algorithm>
#include <optional>

namespace art::usb {

namespace {

constexpr uint8_t kEndpoint0Out = 0x00u;
constexpr uint8_t kEndpoint0In = 0x80u;

// Offsets in the standard descriptors (USB 2.0 tables 9-8, 9-10, 9-12 and 9-13).
constexpr size_t kDeviceDescriptorMaxPacketSize0 = 7u;
constexpr size_t kConfigurationDescriptorSize = 9u;
constexpr size_t kConfigurationNumInterfaces = 4u;
constexpr size_t kConfigurationValue = 5u;
constexpr size_t kConfigurationAttributes = 7u;
constexpr size_t kInterfaceDescriptorSize = 9u;
constexpr size_t kInterfaceNumber = 2u;
constexpr size_t kInterfaceAlternateSetting = 3u;
constexpr size_t kEndpointDescriptorSize = 7u;
constexpr size_t kEndpointAddress = 2u;
constexpr size_t kEndpointAttributes = 3u;
constexpr size_t kEndpointMaxPacketSize = 4u;

// bmAttributes of the configuration descriptor.
constexpr uint8_t kAttributeSelfPowered = 0x40u;
constexpr uint8_t kAttributeRemoteWakeup = 0x20u;

// GET_STATUS reply bits (USB 2.0 figures 9-4 and 9-6).
constexpr uint8_t kStatusSelfPowered = 0x01u;
constexpr uint8_t kStatusRemoteWakeup = 0x02u;
constexpr uint8_t kStatusHalt = 0x01u;

constexpr uint16_t kMaxAddress = 127u;
constexpr uint8_t kDefaultMaxPacketSize0 = 64u;

/// Calls `callback` with each descriptor of a descriptor set. Stops at the first malformed descriptor (bLength
/// below 2 or beyond the end of the set), so a broken set cannot cause a read out of bounds or an endless loop.
template <typename Callback>
void forEachDescriptor(art::span<const uint8_t> descriptorSet, Callback &&callback) {
   size_t offset = 0u;
   while(offset + 2u <= descriptorSet.size()) {
      const size_t length = descriptorSet[offset];
      if(length < 2u || length > descriptorSet.size() - offset) {
         return;
      }
      callback(descriptorSet.subspan(offset, length));
      offset += length;
   }
}

constexpr bool isDescriptorOfType(art::span<const uint8_t> descriptor, DescriptorType type, size_t minimumSize) {
   return descriptor.size() >= minimumSize && descriptor[1] == static_cast<uint8_t>(type);
}

/// Calls `callback` with each endpoint descriptor of one alternate setting of one interface.
template <typename Callback>
void forEachEndpoint(art::span<const uint8_t> configuration, uint8_t interfaceNumber, uint8_t alternateSetting, Callback &&callback) {
   bool inInterface = false;
   forEachDescriptor(configuration, [&](art::span<const uint8_t> descriptor) {
      if(isDescriptorOfType(descriptor, DescriptorType::Interface, kInterfaceDescriptorSize)) {
         inInterface = descriptor[kInterfaceNumber] == interfaceNumber && descriptor[kInterfaceAlternateSetting] == alternateSetting;
      } else if(inInterface && isDescriptorOfType(descriptor, DescriptorType::Endpoint, kEndpointDescriptorSize)) {
         callback(descriptor);
      }
   });
}

/// bMaxPacketSize0 of the device descriptor, or 64 if it is missing or not one of the values USB 2.0 allows.
uint8_t maxPacketSize0Of(art::span<const uint8_t> deviceDescriptor) {
   if(deviceDescriptor.size() <= kDeviceDescriptorMaxPacketSize0) {
      return kDefaultMaxPacketSize0;
   }
   const uint8_t size = deviceDescriptor[kDeviceDescriptorMaxPacketSize0];
   return size == 8u || size == 16u || size == 32u || size == 64u ? size : kDefaultMaxPacketSize0;
}

} // namespace

Device::Device(DeviceController &controller, const Descriptors &descriptors)
   : controller_(controller)
   , descriptors_(descriptors)
   , maxPacketSize0_(maxPacketSize0Of(descriptors.device)) {
   endpointInterface_.fill(kNoInterface);
   controller_.setEventHandler(this);
}

Device::~Device() {
   controller_.setEventHandler(nullptr);
   forEachFunction([](Function &function) { function.device_ = nullptr; });
}

bool Device::addFunction(Function &function) {
   if(function.device_ != nullptr || functionCount_ >= kMaxFunctions || function.interfaceCount() == 0u ||
      function.firstInterface() + function.interfaceCount() > kMaxInterfaces) {
      return false;
   }
   for(size_t index = 0u; index < functionCount_; ++index) {
      const Function &other = *functions_[index];
      if(function.firstInterface() < other.firstInterface() + other.interfaceCount() &&
         other.firstInterface() < function.firstInterface() + function.interfaceCount()) {
         return false;
      }
   }
   functions_[functionCount_++] = &function;
   function.device_ = this;
   return true;
}

void Device::start() {
   if(state_ != DeviceState::Detached) {
      return;
   }
   state_ = DeviceState::Powered;
   controller_.connect();
}

void Device::stop() {
   if(state_ == DeviceState::Detached) {
      return;
   }
   controller_.disconnect();
   deconfigure();
   closeAllEndpoints();
   controller_.closeEndpoint(kEndpoint0Out);
   state_ = DeviceState::Detached;
   address_ = 0u;
   addressPending_ = false;
   remoteWakeupEnabled_ = false;
   controlStage_ = ControlStage::Idle;
}

uint8_t Device::alternateSetting(uint8_t interfaceNumber) const {
   return interfaceNumber < kMaxInterfaces ? alternateSettings_[interfaceNumber] : 0u;
}

bool Device::isEndpointHalted(uint8_t endpointAddress) const {
   return (haltedEndpoints_ & (1u << endpointSlot(endpointAddress))) != 0u;
}

bool Device::startInTransfer(uint8_t endpointAddress, art::span<const uint8_t> packet) {
   if(!isInEndpoint(endpointAddress) || endpointNumber(endpointAddress) == 0u || !isEndpointOpen(endpointAddress)) {
      return false;
   }
   return controller_.startInTransfer(endpointAddress, packet);
}

void Device::armOutTransfer(uint8_t endpointAddress) {
   if(isInEndpoint(endpointAddress) || endpointNumber(endpointAddress) == 0u || !isEndpointOpen(endpointAddress)) {
      return;
   }
   controller_.armOutTransfer(endpointAddress);
}

void Device::haltEndpoint(uint8_t endpointAddress) {
   if(endpointNumber(endpointAddress) == 0u || !isEndpointOpen(endpointAddress)) {
      return;
   }
   controller_.setEndpointStall(endpointAddress, true);
   haltedEndpoints_ |= 1u << endpointSlot(endpointAddress);
}

void Device::busReset() {
   if(state_ == DeviceState::Detached) {
      return;
   }
   if(state_ == DeviceState::Suspended) {
      // A reset ends a suspend as well; the functions see the same resume as for resume signalling.
      resumed();
   }
   deconfigure();
   closeAllEndpoints();
   address_ = 0u;
   addressPending_ = false;
   remoteWakeupEnabled_ = false;
   controlStage_ = ControlStage::Idle;
   state_ = DeviceState::Default;
   controller_.openEndpoint(EndpointConfig{ kEndpoint0Out, EndpointType::Control, maxPacketSize0_ });
}

void Device::suspended() {
   if(state_ == DeviceState::Detached || state_ == DeviceState::Suspended) {
      return;
   }
   stateBeforeSuspend_ = state_;
   state_ = DeviceState::Suspended;
   forEachFunction([](Function &function) { function.suspended(); });
}

void Device::resumed() {
   if(state_ != DeviceState::Suspended) {
      return;
   }
   state_ = stateBeforeSuspend_;
   forEachFunction([](Function &function) { function.resumed(); });
}

void Device::setupReceived(const SetupPacket &setup) {
   if(state_ == DeviceState::Detached) {
      return;
   }
   // A SETUP packet means that the bus is active, even if the controller missed the resume.
   resumed();

   // A new SETUP packet aborts the control transfer before it, including a SET_ADDRESS whose status stage did not
   // complete.
   controlSetup_ = setup;
   controlStage_ = ControlStage::Idle;
   controlInData_ = {};
   controlZlpPending_ = false;
   controlOutReceived_ = 0u;
   addressPending_ = false;

   if(setup.direction() == RequestDirection::HostToDevice && setup.wLength > 0u) {
      if(setup.wLength > kControlBufferSize) {
         stallControl();
         return;
      }
      // The request is handled when the data stage is complete (finishControlOut()).
      controlStage_ = ControlStage::DataOut;
      controller_.armOutTransfer(kEndpoint0Out);
      return;
   }
   if(!handleRequest(setup)) {
      stallControl();
   }
}

void Device::outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) {
   if(endpointAddress != kEndpoint0Out) {
      if(Function *function = functionForEndpoint(endpointAddress)) {
         function->outPacketReceived(endpointAddress, packet);
      }
      return;
   }

   switch(controlStage_) {
   case ControlStage::DataOut: {
      const size_t count = std::min<size_t>(packet.size(), controlSetup_.wLength - controlOutReceived_);
      std::copy_n(packet.begin(), count, controlBuffer_.begin() + controlOutReceived_);
      controlOutReceived_ = static_cast<uint16_t>(controlOutReceived_ + count);
      // A short packet ends the data stage early.
      if(controlOutReceived_ >= controlSetup_.wLength || packet.size() < maxPacketSize0_) {
         finishControlOut();
      } else {
         controller_.armOutTransfer(kEndpoint0Out);
      }
      break;
   }
   case ControlStage::StatusOut:
   case ControlStage::DataIn:
      // The status stage of a device-to-host transfer. During DataIn the host ended the data stage early, or the
      // event of the last data packet is still on its way. A data packet that is still queued on EP0 IN is never
      // read: the controller drops it on the next SETUP packet (ControllerEvents::setupReceived()).
      controlStage_ = ControlStage::Idle;
      controlInData_ = {};
      controlZlpPending_ = false;
      break;
   case ControlStage::Idle:
   case ControlStage::StatusIn:
      break;
   }
}

void Device::inPacketSent(uint8_t endpointAddress) {
   if(endpointAddress != kEndpoint0In) {
      if(Function *function = functionForEndpoint(endpointAddress)) {
         function->inPacketSent(endpointAddress);
      }
      return;
   }

   switch(controlStage_) {
   case ControlStage::DataIn:
      if(!controlInData_.empty() || controlZlpPending_) {
         sendNextControlPacket();
      } else {
         startStatusOut();
      }
      break;
   case ControlStage::StatusIn:
      controlStage_ = ControlStage::Idle;
      // USB 2.0 section 9.4.6: the device uses the new address only after the status stage of SET_ADDRESS. The
      // controller has already applied it in hardware (DeviceController::setAddress()).
      if(addressPending_) {
         addressPending_ = false;
         address_ = pendingAddress_;
         state_ = address_ != 0u ? DeviceState::Address : DeviceState::Default;
      }
      break;
   case ControlStage::Idle:
   case ControlStage::DataOut:
   case ControlStage::StatusOut:
      break;
   }
}

size_t Device::endpointSlot(uint8_t endpointAddress) {
   return endpointNumber(endpointAddress) + (isInEndpoint(endpointAddress) ? kMaxEndpointNumbers : 0u);
}

bool Device::handleRequest(const SetupPacket &setup) {
   switch(setup.type()) {
   case RequestType::Standard:
      return handleStandardRequest(setup);
   case RequestType::Class:
   case RequestType::Vendor:
      if(state_ != DeviceState::Configured) {
         return false;
      }
      if(setup.recipient() == RequestRecipient::Interface) {
         return forwardToFunction(setup, functionForInterface(static_cast<uint8_t>(setup.wIndex & 0xFFu)));
      }
      if(setup.recipient() == RequestRecipient::Endpoint) {
         return forwardToFunction(setup, functionForEndpoint(static_cast<uint8_t>(setup.wIndex & 0xFFu)));
      }
      return false;
   case RequestType::Reserved:
      break;
   }
   return false;
}

bool Device::handleStandardRequest(const SetupPacket &setup) {
   switch(setup.recipient()) {
   case RequestRecipient::Device:
      return handleDeviceRequest(setup);
   case RequestRecipient::Interface:
      return handleInterfaceRequest(setup);
   case RequestRecipient::Endpoint:
      return handleEndpointRequest(setup);
   case RequestRecipient::Other:
      break;
   }
   return false;
}

bool Device::handleDeviceRequest(const SetupPacket &setup) {
   const bool deviceToHost = setup.direction() == RequestDirection::DeviceToHost;
   const bool addressed = state_ == DeviceState::Address || state_ == DeviceState::Configured;

   switch(static_cast<StandardRequest>(setup.bRequest)) {
   case StandardRequest::GetStatus: {
      if(!deviceToHost || !addressed || setup.wValue != 0u || setup.wIndex != 0u) {
         return false;
      }
      const uint8_t attributes = configurationAttributes();
      controlBuffer_[0] = static_cast<uint8_t>(((attributes & kAttributeSelfPowered) != 0u ? kStatusSelfPowered : 0u) |
                                               (remoteWakeupEnabled_ ? kStatusRemoteWakeup : 0u));
      controlBuffer_[1] = 0u;
      sendControlData(art::span<const uint8_t>(controlBuffer_.data(), 2u));
      return true;
   }
   case StandardRequest::ClearFeature:
   case StandardRequest::SetFeature:
      // TEST_MODE is for high-speed devices only. DEVICE_REMOTE_WAKEUP only if the configuration declares it.
      if(deviceToHost || !addressed || setup.wIndex != 0u || setup.wLength != 0u ||
         setup.wValue != static_cast<uint16_t>(FeatureSelector::DeviceRemoteWakeup) ||
         (configurationAttributes() & kAttributeRemoteWakeup) == 0u) {
         return false;
      }
      remoteWakeupEnabled_ = setup.isStandard(StandardRequest::SetFeature);
      startStatusIn();
      return true;
   case StandardRequest::SetAddress:
      // USB 2.0 does not define SET_ADDRESS in the Configured state.
      if(deviceToHost || setup.wValue > kMaxAddress || setup.wIndex != 0u || setup.wLength != 0u ||
         (state_ != DeviceState::Default && state_ != DeviceState::Address)) {
         return false;
      }
      pendingAddress_ = static_cast<uint8_t>(setup.wValue);
      addressPending_ = true;
      // The controller applies the address when the host acknowledges the status stage. It must know the address
      // now: the main loop sees that acknowledgement later, and the host uses the new address 2 ms after it.
      controller_.setAddress(pendingAddress_);
      startStatusIn();
      return true;
   case StandardRequest::GetDescriptor:
      return deviceToHost && handleGetDescriptor(setup);
   case StandardRequest::GetConfiguration:
      if(!deviceToHost || !addressed || setup.wValue != 0u || setup.wIndex != 0u) {
         return false;
      }
      controlBuffer_[0] = configuration_;
      sendControlData(art::span<const uint8_t>(controlBuffer_.data(), 1u));
      return true;
   case StandardRequest::SetConfiguration:
      return !deviceToHost && handleSetConfiguration(setup);
   default:
      // SET_DESCRIPTOR, and the interface and endpoint requests sent to the device.
      return false;
   }
}

bool Device::handleGetDescriptor(const SetupPacket &setup) {
   const auto type = static_cast<DescriptorType>(setup.wValue >> 8u);
   const uint8_t index = static_cast<uint8_t>(setup.wValue & 0xFFu);

   switch(type) {
   case DescriptorType::Device:
      if(descriptors_.device.empty()) {
         return false;
      }
      sendControlData(descriptors_.device);
      return true;
   case DescriptorType::Configuration:
      // The device has a single configuration, index 0.
      if(index != 0u || descriptors_.configuration.empty()) {
         return false;
      }
      sendControlData(descriptors_.configuration);
      return true;
   case DescriptorType::String:
      if(index >= descriptors_.strings.size() || descriptors_.strings[index].empty()) {
         return false;
      }
      sendControlData(descriptors_.strings[index]);
      return true;
   default:
      // A full-speed-only device stalls DEVICE_QUALIFIER and OTHER_SPEED_CONFIGURATION (USB 2.0 section 9.6.2).
      // Interface and endpoint descriptors cannot be read on their own.
      return false;
   }
}

bool Device::handleSetConfiguration(const SetupPacket &setup) {
   if(setup.wValue > 0xFFu || setup.wIndex != 0u || setup.wLength != 0u ||
      (state_ != DeviceState::Address && state_ != DeviceState::Configured)) {
      return false;
   }
   const auto value = static_cast<uint8_t>(setup.wValue);
   if(value != 0u && (value != configurationValue() || interfaceCount() > kMaxInterfaces)) {
      return false;
   }

   // Also for the active configuration: SET_CONFIGURATION resets the halt and the data toggle of every endpoint.
   deconfigure();
   state_ = DeviceState::Address;
   if(value != 0u) {
      for(uint8_t interfaceNumber = 0u; interfaceNumber < interfaceCount(); ++interfaceNumber) {
         if(!openInterfaceEndpoints(interfaceNumber, 0u)) {
            closeAllEndpoints();
            return false;
         }
      }
      configuration_ = value;
      state_ = DeviceState::Configured;
      forEachFunction([](Function &function) { function.configured(); });
   }
   startStatusIn();
   return true;
}

bool Device::handleInterfaceRequest(const SetupPacket &setup) {
   const bool deviceToHost = setup.direction() == RequestDirection::DeviceToHost;
   const auto interfaceNumber = static_cast<uint8_t>(setup.wIndex & 0xFFu);
   // Interfaces exist only in the Configured state.
   if(state_ != DeviceState::Configured || setup.wIndex > 0xFFu || interfaceNumber >= interfaceCount()) {
      return false;
   }

   switch(static_cast<StandardRequest>(setup.bRequest)) {
   case StandardRequest::GetStatus:
      if(!deviceToHost || setup.wValue != 0u) {
         return false;
      }
      controlBuffer_[0] = 0u;
      controlBuffer_[1] = 0u;
      sendControlData(art::span<const uint8_t>(controlBuffer_.data(), 2u));
      return true;
   case StandardRequest::GetInterface:
      if(!deviceToHost || setup.wValue != 0u) {
         return false;
      }
      controlBuffer_[0] = alternateSettings_[interfaceNumber];
      sendControlData(art::span<const uint8_t>(controlBuffer_.data(), 1u));
      return true;
   case StandardRequest::SetInterface:
      return !deviceToHost && handleSetInterface(setup);
   case StandardRequest::ClearFeature:
   case StandardRequest::SetFeature:
      // USB 2.0 defines no interface features.
      return false;
   default:
      // For example GET_DESCRIPTOR of a class-specific descriptor (HID report descriptor).
      return forwardToFunction(setup, functionForInterface(interfaceNumber));
   }
}

bool Device::handleSetInterface(const SetupPacket &setup) {
   const auto interfaceNumber = static_cast<uint8_t>(setup.wIndex);
   if(setup.wValue > 0xFFu || setup.wLength != 0u) {
      return false;
   }
   const auto alternateSetting = static_cast<uint8_t>(setup.wValue);
   if(!hasAlternateSetting(interfaceNumber, alternateSetting)) {
      return false;
   }

   // Also for the active setting: SET_INTERFACE resets the halt and the data toggle of its endpoints.
   const uint8_t previousSetting = alternateSettings_[interfaceNumber];
   closeInterfaceEndpoints(interfaceNumber);
   if(!openInterfaceEndpoints(interfaceNumber, alternateSetting)) {
      closeInterfaceEndpoints(interfaceNumber);
      // The previous setting fitted before, so reopening it normally succeeds.
      openInterfaceEndpoints(interfaceNumber, previousSetting);
      return false;
   }
   alternateSettings_[interfaceNumber] = alternateSetting;
   if(Function *function = functionForInterface(interfaceNumber)) {
      function->alternateSettingChanged(interfaceNumber, alternateSetting);
   }
   startStatusIn();
   return true;
}

bool Device::handleEndpointRequest(const SetupPacket &setup) {
   const bool deviceToHost = setup.direction() == RequestDirection::DeviceToHost;
   const auto endpointAddress = static_cast<uint8_t>(setup.wIndex & 0xFFu);
   const bool isEndpoint0 = endpointNumber(endpointAddress) == 0u;
   // EP0 exists from the Address state on, the other endpoints while they are open in the Configured state.
   const bool exists = isEndpoint0 ? state_ == DeviceState::Address || state_ == DeviceState::Configured :
                                     state_ == DeviceState::Configured && isEndpointOpen(endpointAddress);
   if(!exists || setup.wIndex > 0xFFu || (endpointAddress & 0x70u) != 0u) {
      return false;
   }

   switch(static_cast<StandardRequest>(setup.bRequest)) {
   case StandardRequest::GetStatus:
      if(!deviceToHost || setup.wValue != 0u) {
         return false;
      }
      controlBuffer_[0] = !isEndpoint0 && isEndpointHalted(endpointAddress) ? kStatusHalt : 0u;
      controlBuffer_[1] = 0u;
      sendControlData(art::span<const uint8_t>(controlBuffer_.data(), 2u));
      return true;
   case StandardRequest::ClearFeature:
      if(deviceToHost || setup.wLength != 0u || setup.wValue != static_cast<uint16_t>(FeatureSelector::EndpointHalt)) {
         return false;
      }
      if(!isEndpoint0) {
         controller_.setEndpointStall(endpointAddress, false);
         haltedEndpoints_ &= ~(1u << endpointSlot(endpointAddress));
         if(Function *function = functionForEndpoint(endpointAddress)) {
            function->endpointHaltCleared(endpointAddress);
         }
      }
      startStatusIn();
      return true;
   case StandardRequest::SetFeature:
      // Halting EP0 is not recommended (USB 2.0 section 9.4.5); a protocol stall answers it.
      if(deviceToHost || isEndpoint0 || setup.wLength != 0u || setup.wValue != static_cast<uint16_t>(FeatureSelector::EndpointHalt)) {
         return false;
      }
      haltEndpoint(endpointAddress);
      startStatusIn();
      return true;
   default:
      // SYNCH_FRAME is for isochronous endpoints, which the device does not support.
      return false;
   }
}

bool Device::forwardToFunction(const SetupPacket &setup, Function *function) {
   if(function == nullptr) {
      return false;
   }
   if(setup.direction() == RequestDirection::DeviceToHost) {
      const size_t capacity = std::min<size_t>(setup.wLength, kControlBufferSize);
      const std::optional<uint16_t> replyLength =
      function->controlRequest(setup, art::span<uint8_t>(controlBuffer_.data(), capacity));
      if(!replyLength.has_value()) {
         return false;
      }
      sendControlData(art::span<const uint8_t>(controlBuffer_.data(), std::min<size_t>(*replyLength, capacity)));
      return true;
   }
   if(!function->controlRequest(setup, art::span<uint8_t>(controlBuffer_.data(), controlOutReceived_)).has_value()) {
      return false;
   }
   startStatusIn();
   return true;
}

void Device::sendControlData(art::span<const uint8_t> data) {
   // Without a data stage (wLength 0) the status stage is IN, also for a device-to-host request.
   if(controlSetup_.wLength == 0u) {
      startStatusIn();
      return;
   }
   const size_t length = std::min<size_t>(data.size(), controlSetup_.wLength);
   controlInData_ = data.first(length);
   // The host stops reading at wLength bytes or at a short packet. A reply that is shorter than wLength and fills
   // its last packet needs a zero-length packet to tell the host that it ended.
   // maxPacketSize0_ is a power of two (maxPacketSize0Of()), so the mask is the remainder.
   controlZlpPending_ = length < controlSetup_.wLength && (length & (maxPacketSize0_ - 1u)) == 0u;
   controlStage_ = ControlStage::DataIn;
   // Accept the status stage from the start of the data stage: the host may end the data stage early, and with
   // queued events the host sends the status before the main loop has seen the last data packet acknowledged.
   controller_.armOutTransfer(kEndpoint0Out);
   sendNextControlPacket();
}

void Device::sendNextControlPacket() {
   const size_t length = std::min<size_t>(controlInData_.size(), maxPacketSize0_);
   const art::span<const uint8_t> packet = controlInData_.first(length);
   controlInData_ = controlInData_.subspan(length);
   if(length == 0u) {
      controlZlpPending_ = false;
   }
   if(!controller_.startInTransfer(kEndpoint0In, packet)) {
      stallControl();
   }
}

void Device::startStatusIn() {
   controlStage_ = ControlStage::StatusIn;
   if(!controller_.startInTransfer(kEndpoint0In, art::span<const uint8_t>())) {
      stallControl();
   }
}

void Device::startStatusOut() {
   controlStage_ = ControlStage::StatusOut;
   controller_.armOutTransfer(kEndpoint0Out);
}

void Device::stallControl() {
   controller_.setEndpointStall(kEndpoint0In, true);
   controller_.setEndpointStall(kEndpoint0Out, true);
   controlStage_ = ControlStage::Idle;
   controlInData_ = {};
   controlZlpPending_ = false;
   addressPending_ = false;
}

void Device::finishControlOut() {
   if(!handleRequest(controlSetup_)) {
      stallControl();
   }
}

bool Device::openInterfaceEndpoints(uint8_t interfaceNumber, uint8_t alternateSetting) {
   bool success = true;
   forEachEndpoint(descriptors_.configuration, interfaceNumber, alternateSetting, [&](art::span<const uint8_t> descriptor) {
      const uint8_t endpointAddress = descriptor[kEndpointAddress];
      const size_t slot = endpointSlot(endpointAddress);
      // Endpoint 0 and reserved address bits cannot appear in an endpoint descriptor; an endpoint used twice is a
      // broken descriptor as well.
      if(!success || endpointNumber(endpointAddress) == 0u || (endpointAddress & 0x70u) != 0u ||
         endpointInterface_[slot] != kNoInterface) {
         success = false;
         return;
      }
      const EndpointConfig config{
         endpointAddress, static_cast<EndpointType>(descriptor[kEndpointAttributes] & 0x03u),
         static_cast<uint16_t>((descriptor[kEndpointMaxPacketSize] | (descriptor[kEndpointMaxPacketSize + 1u] << 8u)) & 0x07FFu)
      };
      if(!controller_.openEndpoint(config)) {
         success = false;
         return;
      }
      endpointInterface_[slot] = interfaceNumber;
      haltedEndpoints_ &= ~(1u << slot);
   });
   return success;
}

void Device::closeInterfaceEndpoints(uint8_t interfaceNumber) {
   for(size_t slot = 0u; slot < kEndpointSlots; ++slot) {
      if(endpointInterface_[slot] == interfaceNumber) {
         const auto number = static_cast<uint8_t>(slot % kMaxEndpointNumbers);
         controller_.closeEndpoint(slot < kMaxEndpointNumbers ? number : number | kEndpointDirectionIn);
         endpointInterface_[slot] = kNoInterface;
         haltedEndpoints_ &= ~(1u << slot);
      }
   }
}

void Device::closeAllEndpoints() {
   for(uint8_t interfaceNumber = 0u; interfaceNumber < kMaxInterfaces; ++interfaceNumber) {
      closeInterfaceEndpoints(interfaceNumber);
   }
   haltedEndpoints_ = 0u;
}

void Device::deconfigure() {
   if(configuration_ == 0u) {
      return;
   }
   closeAllEndpoints();
   configuration_ = 0u;
   alternateSettings_.fill(0u);
   forEachFunction([](Function &function) { function.deconfigured(); });
}

bool Device::hasAlternateSetting(uint8_t interfaceNumber, uint8_t alternateSetting) const {
   bool found = false;
   forEachDescriptor(descriptors_.configuration, [&](art::span<const uint8_t> descriptor) {
      if(isDescriptorOfType(descriptor, DescriptorType::Interface, kInterfaceDescriptorSize) &&
         descriptor[kInterfaceNumber] == interfaceNumber && descriptor[kInterfaceAlternateSetting] == alternateSetting) {
         found = true;
      }
   });
   return found;
}

uint8_t Device::configurationAttributes() const {
   return descriptors_.configuration.size() >= kConfigurationDescriptorSize ?
          descriptors_.configuration[kConfigurationAttributes] :
          0u;
}

uint8_t Device::configurationValue() const {
   return descriptors_.configuration.size() >= kConfigurationDescriptorSize ? descriptors_.configuration[kConfigurationValue] : 0u;
}

uint8_t Device::interfaceCount() const {
   return descriptors_.configuration.size() >= kConfigurationDescriptorSize ?
          descriptors_.configuration[kConfigurationNumInterfaces] :
          0u;
}

Function *Device::functionForInterface(uint8_t interfaceNumber) const {
   for(size_t index = 0u; index < functionCount_; ++index) {
      if(functions_[index]->ownsInterface(interfaceNumber)) {
         return functions_[index];
      }
   }
   return nullptr;
}

Function *Device::functionForEndpoint(uint8_t endpointAddress) const {
   if((endpointAddress & 0x70u) != 0u) {
      return nullptr;
   }
   const uint8_t interfaceNumber = endpointInterface_[endpointSlot(endpointAddress)];
   return interfaceNumber == kNoInterface ? nullptr : functionForInterface(interfaceNumber);
}

bool Device::isEndpointOpen(uint8_t endpointAddress) const {
   return (endpointAddress & 0x70u) == 0u && endpointInterface_[endpointSlot(endpointAddress)] != kNoInterface;
}

} // namespace art::usb
