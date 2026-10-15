#pragma once

#include "usb/DeviceController.h"
#include "utils/compat/span.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace art::usb::test {

/**
 * @brief DeviceController test double that records what the protocol layer asks for, and a host that drives it.
 *
 * The controller part models the packet-level contract of DeviceController:
 * - an open endpoint NAKs until it is armed (OUT) or has a packet (IN);
 * - one IN packet per endpoint at a time: startInTransfer() fails while the last one is not acknowledged;
 * - a SETUP packet ends a stall of EP0 and sets both directions of EP0 to NAK (drops a queued IN packet and the
 *   OUT arm) before it is reported;
 * - startInTransfer() and armOutTransfer() on EP0 end a protocol stall of EP0;
 * - setEndpointStall() (true and false) drops a queued IN packet and the OUT arm of the endpoint;
 * - cancelInTransfer() drops a queued IN packet. It returns false while the event of a packet that the host took
 *   waits in the queue (see "Queued events");
 * - setAddress() keeps the address pending until the host acknowledges the zero-length status packet on EP0; a
 *   SETUP packet or a bus reset drops it.
 *
 * The host part (bus*(), controlIn(), controlOut(), sendOut(), takeIn()) plays the host side of the bus and checks
 * the device against the rules of the host: it reads a control data stage until wLength bytes or a short packet, and
 * it only sends OUT packets to armed endpoints.
 *
 * @par Queued events
 * By default an event reaches the handler at once. With queueEvents(true), the fake behaves like a controller that
 * queues the events in its interrupt handler: the effect on the hardware state happens at once (NAK on SETUP, the
 * address on the status acknowledgement, the endpoints on a bus reset), and the handler gets the events, in order,
 * only from deliverQueuedEvents(), the main loop. takeIn() and sendOut() never deliver. controlIn() and controlOut()
 * deliver only when the host would otherwise wait for a NAKed endpoint, so the main loop runs as late as possible.
 */
class FakeDeviceController : public DeviceController {
   public:
   /// Result of a control transfer that the host ran.
   struct ControlResult {
      /// The device stalled the data or status stage.
      bool stalled{ false };
      /// The data stage that the host read (device-to-host only).
      std::vector<uint8_t> data;
      /// The size of each data packet that the host read, including a zero-length packet.
      std::vector<size_t> packetSizes;
   };

   void setEventHandler(ControllerEvents *handler) override {
      handler_ = handler;
   }

   void connect() override {
      connected_ = true;
   }

   void disconnect() override {
      connected_ = false;
   }

   /// Like an STM32 FS peripheral: the address is pending until the host acknowledges the status stage on EP0.
   void setAddress(uint8_t address) override {
      pendingAddress_ = address;
      ++setAddressCalls_;
   }

   bool openEndpoint(const EndpointConfig &config) override {
      if(failOpenOf_.has_value() && *failOpenOf_ == config.address) {
         return false;
      }
      endpoints_[config.address] = config;
      if(config.type == EndpointType::Control) {
         // A control endpoint covers both directions.
         endpoints_[static_cast<uint8_t>(config.address | kEndpointDirectionIn)] = config;
      }
      return true;
   }

   void closeEndpoint(uint8_t endpointAddress) override {
      endpoints_.erase(endpointAddress);
      if(endpointNumber(endpointAddress) == 0u) {
         endpoints_.erase(kEndpointDirectionIn);
      }
      stalled_.erase(endpointAddress);
      armed_.erase(endpointAddress);
      inPackets_.erase(endpointAddress);
   }

   void setEndpointStall(uint8_t endpointAddress, bool stall) override {
      armed_.erase(endpointAddress);
      inPackets_.erase(endpointAddress);
      if(stall) {
         stalled_.insert(endpointAddress);
      } else {
         stalled_.erase(endpointAddress);
         ++toggleResets_[endpointAddress];
      }
   }

   bool startInTransfer(uint8_t endpointAddress, art::span<const uint8_t> packet) override {
      const auto endpoint = endpoints_.find(endpointAddress);
      if(endpoint == endpoints_.end() || inPackets_.count(endpointAddress) != 0u || packet.size() > endpoint->second.maxPacketSize) {
         return false;
      }
      if(endpointNumber(endpointAddress) == 0u) {
         stalled_.erase(endpointAddress);
      }
      inPackets_[endpointAddress] = std::vector<uint8_t>(packet.begin(), packet.end());
      return true;
   }

   bool cancelInTransfer(uint8_t endpointAddress) override {
      inPackets_.erase(endpointAddress);
      const auto waiting = inEventsQueued_.find(endpointAddress);
      return waiting == inEventsQueued_.end() || waiting->second == 0;
   }

   void armOutTransfer(uint8_t endpointAddress) override {
      if(endpoints_.count(endpointAddress) == 0u) {
         return;
      }
      if(endpointNumber(endpointAddress) == 0u) {
         stalled_.erase(endpointAddress);
      }
      armed_.insert(endpointAddress);
   }

   // ---- State for checks ----

   [[nodiscard]] bool isConnected() const {
      return connected_;
   }

   [[nodiscard]] uint8_t address() const {
      return address_;
   }

   /// The address that setAddress() requested and that waits for the status stage, if any.
   [[nodiscard]] std::optional<uint8_t> pendingAddress() const {
      return pendingAddress_;
   }

   [[nodiscard]] int setAddressCalls() const {
      return setAddressCalls_;
   }

   [[nodiscard]] bool isOpen(uint8_t endpointAddress) const {
      return endpoints_.count(endpointAddress) != 0u;
   }

   [[nodiscard]] std::optional<EndpointConfig> endpoint(uint8_t endpointAddress) const {
      const auto endpoint = endpoints_.find(endpointAddress);
      if(endpoint == endpoints_.end()) {
         return std::nullopt;
      }
      return endpoint->second;
   }

   [[nodiscard]] size_t openEndpointCount() const {
      return endpoints_.size();
   }

   [[nodiscard]] bool isStalled(uint8_t endpointAddress) const {
      return stalled_.count(endpointAddress) != 0u;
   }

   [[nodiscard]] bool isArmed(uint8_t endpointAddress) const {
      return armed_.count(endpointAddress) != 0u;
   }

   [[nodiscard]] bool hasInPacket(uint8_t endpointAddress) const {
      return inPackets_.count(endpointAddress) != 0u;
   }

   /// How often setEndpointStall(false) reset the data toggle of the endpoint.
   [[nodiscard]] int toggleResets(uint8_t endpointAddress) const {
      const auto count = toggleResets_.find(endpointAddress);
      return count == toggleResets_.end() ? 0 : count->second;
   }

   /// Makes openEndpoint() fail for one endpoint address, as a full packet memory would.
   void failOpenOf(std::optional<uint8_t> endpointAddress) {
      failOpenOf_ = endpointAddress;
   }

   // ---- Host side ----

   void busReset() {
      endpoints_.clear();
      stalled_.clear();
      armed_.clear();
      inPackets_.clear();
      address_ = 0u;
      pendingAddress_.reset();
      emit([this] { handler_->busReset(); });
   }

   void busSuspend() {
      emit([this] { handler_->suspended(); });
   }

   void busResume() {
      emit([this] { handler_->resumed(); });
   }

   /// Sends a SETUP packet, with the effect on EP0 that the contract describes.
   void sendSetup(const SetupPacket &setup) {
      stalled_.erase(0x00u);
      stalled_.erase(kEndpointDirectionIn);
      armed_.erase(0x00u);
      inPackets_.erase(kEndpointDirectionIn);
      pendingAddress_.reset();
      emit([this, setup] { handler_->setupReceived(setup); });
   }

   /// Sends an OUT packet. Returns false (and sends nothing) if the endpoint is not armed.
   bool sendOut(uint8_t endpointAddress, art::span<const uint8_t> packet) {
      if(armed_.erase(endpointAddress) == 0u || isStalled(endpointAddress)) {
         return false;
      }
      emit([this, endpointAddress, data = std::vector<uint8_t>(packet.begin(), packet.end())] {
         handler_->outPacketReceived(endpointAddress, art::span<const uint8_t>(data.data(), data.size()));
      });
      return true;
   }

   /// Reads and acknowledges the pending IN packet of an endpoint, or returns nullopt if the endpoint NAKs.
   std::optional<std::vector<uint8_t>> takeIn(uint8_t endpointAddress) {
      const auto pending = inPackets_.find(endpointAddress);
      if(pending == inPackets_.end() || isStalled(endpointAddress)) {
         return std::nullopt;
      }
      std::vector<uint8_t> packet = std::move(pending->second);
      inPackets_.erase(pending);
      // The acknowledgement of the status stage applies a pending address at once, as the interrupt handler of a
      // real controller does, before the event reaches the protocol layer.
      if(endpointAddress == kEndpointDirectionIn && packet.empty() && pendingAddress_.has_value()) {
         address_ = *pendingAddress_;
         pendingAddress_.reset();
      }
      const bool queued = queueEvents_;
      if(queued) {
         ++inEventsQueued_[endpointAddress];
      }
      emit([this, endpointAddress, queued] {
         if(queued) {
            --inEventsQueued_[endpointAddress];
         }
         handler_->inPacketSent(endpointAddress);
      });
      return packet;
   }

   /// Queues the events until deliverQueuedEvents() (true), or delivers them at once (false, the default). See
   /// "Queued events" above.
   void queueEvents(bool queue) {
      queueEvents_ = queue;
   }

   /// The main loop: delivers the queued events in order, including the events that they cause.
   void deliverQueuedEvents() {
      while(!queuedEvents_.empty()) {
         std::vector<std::function<void()>> events;
         events.swap(queuedEvents_);
         for(const std::function<void()> &event : events) {
            event();
         }
      }
   }

   [[nodiscard]] size_t queuedEventCount() const {
      return queuedEvents_.size();
   }

   /**
    * @brief Runs a control transfer with a device-to-host data stage (or no data stage), as a host does.
    *
    * Reads packets until wLength bytes or a short packet arrived, then sends the zero-length status packet. The
    * transfer fails the test (`ok` false) if the device NAKs where the host expects data.
    */
   ControlResult controlIn(const SetupPacket &setup, bool *ok = nullptr) {
      ControlResult result;
      bool success = true;
      const size_t maxPacketSize = endpointMaxPacketSize0();
      sendSetup(setup);
      if(setup.wLength == 0u) {
         // No data stage: the status stage is IN.
         return finishWithStatusIn(result, ok);
      }
      while(true) {
         std::optional<std::vector<uint8_t>> packet = takeInWaiting(kEndpointDirectionIn);
         if(!packet.has_value()) {
            result.stalled = isStalled(kEndpointDirectionIn);
            success = result.stalled;
            break;
         }
         result.packetSizes.push_back(packet->size());
         result.data.insert(result.data.end(), packet->begin(), packet->end());
         if(result.data.size() >= setup.wLength || packet->size() < maxPacketSize) {
            break;
         }
      }
      if(!result.stalled && success) {
         // Status stage: a zero-length OUT packet. A device that still has data queued sent too much.
         success = !hasInPacket(kEndpointDirectionIn) && sendOutWaiting(0x00u, art::span<const uint8_t>());
      }
      if(ok != nullptr) {
         *ok = success;
      }
      return result;
   }

   /// Runs a control transfer with a host-to-device data stage (`data`, which must have setup.wLength bytes).
   ControlResult controlOut(const SetupPacket &setup, art::span<const uint8_t> data, bool *ok = nullptr) {
      ControlResult result;
      const size_t maxPacketSize = endpointMaxPacketSize0();
      sendSetup(setup);
      size_t sent = 0u;
      while(sent < data.size()) {
         const size_t length = std::min(maxPacketSize, data.size() - sent);
         if(!sendOutWaiting(0x00u, data.subspan(sent, length))) {
            // A stall ends the transfer as the device wants it; a NAK forever is a failure.
            result.stalled = isStalled(0x00u);
            if(ok != nullptr) {
               *ok = result.stalled;
            }
            return result;
         }
         sent += length;
      }
      return finishWithStatusIn(result, ok);
   }

   private:
   /// Delivers the event at once, or queues it (queueEvents()).
   void emit(std::function<void()> event) {
      if(queueEvents_) {
         queuedEvents_.push_back(std::move(event));
      } else {
         event();
      }
   }

   /// takeIn(), but if the endpoint NAKs, the main loop runs once (the queued events are delivered) and the host
   /// tries again.
   std::optional<std::vector<uint8_t>> takeInWaiting(uint8_t endpointAddress) {
      std::optional<std::vector<uint8_t>> packet = takeIn(endpointAddress);
      if(!packet.has_value() && !queuedEvents_.empty()) {
         deliverQueuedEvents();
         packet = takeIn(endpointAddress);
      }
      return packet;
   }

   /// sendOut(), with the same retry as takeInWaiting().
   bool sendOutWaiting(uint8_t endpointAddress, art::span<const uint8_t> packet) {
      if(sendOut(endpointAddress, packet)) {
         return true;
      }
      if(queuedEvents_.empty()) {
         return false;
      }
      deliverQueuedEvents();
      return sendOut(endpointAddress, packet);
   }

   /// The EP0 packet size. After a queued bus reset the device has not opened EP0 yet, so the main loop runs first.
   size_t endpointMaxPacketSize0() {
      if(endpoints_.count(0x00u) == 0u) {
         deliverQueuedEvents();
      }
      const auto endpoint = endpoints_.find(0x00u);
      return endpoint == endpoints_.end() ? 0u : endpoint->second.maxPacketSize;
   }

   ControlResult &finishWithStatusIn(ControlResult &result, bool *ok) {
      bool success = true;
      std::optional<std::vector<uint8_t>> status = takeInWaiting(kEndpointDirectionIn);
      if(status.has_value()) {
         success = status->empty();
      } else {
         result.stalled = isStalled(kEndpointDirectionIn);
         success = result.stalled;
      }
      if(ok != nullptr) {
         *ok = success;
      }
      return result;
   }

   ControllerEvents *handler_{ nullptr };
   bool connected_{ false };
   uint8_t address_{ 0u };
   int setAddressCalls_{ 0 };
   std::optional<uint8_t> pendingAddress_;
   bool queueEvents_{ false };
   std::vector<std::function<void()>> queuedEvents_;
   std::map<uint8_t, EndpointConfig> endpoints_;
   std::set<uint8_t> stalled_;
   std::set<uint8_t> armed_;
   std::map<uint8_t, std::vector<uint8_t>> inPackets_;
   std::map<uint8_t, int> toggleResets_;
   // inPacketSent() events in the queue, for each endpoint
   std::map<uint8_t, int> inEventsQueued_;
   std::optional<uint8_t> failOpenOf_;
};

/// Builds a SETUP packet.
constexpr SetupPacket setupPacket(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength) {
   SetupPacket setup{};
   setup.bmRequestType = bmRequestType;
   setup.bRequest = bRequest;
   setup.wValue = wValue;
   setup.wIndex = wIndex;
   setup.wLength = wLength;
   return setup;
}

} // namespace art::usb::test
