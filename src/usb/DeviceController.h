#pragma once

#include "usb/UsbTypes.h"
#include "utils/compat/span.h"
#include <cstdint>

namespace art::usb {

/**
 * @brief Events that a DeviceController reports to the protocol layer (usb::Device).
 *
 * The controller calls these from the context that runs the protocol logic, normally the main loop, not from the
 * interrupt handler. A controller that takes the events in an interrupt queues them and delivers them later (see
 * "Threading" in docs/usb-device-stack.md). The calls for one controller must not overlap.
 */
class ControllerEvents {
   public:
   virtual ~ControllerEvents() = default;

   /// The host drove a bus reset. The controller has set the device address to 0 and closed all endpoints.
   virtual void busReset() = 0;

   /// The bus was idle for 3 ms.
   virtual void suspended() = 0;

   /// Bus activity after a suspend.
   virtual void resumed() = 0;

   /**
    * @brief A SETUP packet arrived on endpoint 0.
    *
    * The controller accepts a SETUP packet also when EP0 is stalled or NAKs. Before it reports the packet, it sets
    * both directions of EP0 to NAK, so that a data or status stage of an earlier, aborted transfer cannot go out.
    */
   virtual void setupReceived(const SetupPacket &setup) = 0;

   /**
    * @brief One OUT packet arrived on an endpoint that DeviceController::armOutTransfer() armed.
    *
    * After the packet the endpoint NAKs again until the next armOutTransfer().
    *
    * @param endpointAddress The OUT endpoint address (bit 7 clear).
    * @param packet The packet data; empty for a zero-length packet. Valid only during the call.
    */
   virtual void outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) = 0;

   /**
    * @brief The host acknowledged the packet of the last DeviceController::startInTransfer() on this endpoint.
    *
    * @param endpointAddress The IN endpoint address (bit 7 set).
    */
   virtual void inPacketSent(uint8_t endpointAddress) = 0;

   protected:
   ControllerEvents() = default;
   ControllerEvents(const ControllerEvents &) = default;
   ControllerEvents &operator=(const ControllerEvents &) = default;
};

/**
 * @brief Hardware-independent interface to a USB full-speed device peripheral.
 *
 * usb::Device runs the chapter 9 protocol on top of this interface, so the protocol code has no hardware
 * dependency and runs in host tests against a fake controller. A port implements it on the register driver of its
 * MCU (for example libart-stm32 on hw::UsbDevice of system-stm32).
 *
 * The interface works on single packets: startInTransfer() sends one packet and armOutTransfer() accepts one
 * packet. Splitting a transfer into packets, short packets and zero-length packets (ZLP) are the job of the
 * protocol layer. An endpoint that is open but not armed answers the host with NAK.
 *
 * @par Endpoint halt
 * setEndpointStall(true) makes the endpoint answer every token with STALL. setEndpointStall(false) ends that and
 * resets the data toggle of the endpoint to DATA0 (as CLEAR_FEATURE(ENDPOINT_HALT) requires). On EP0 a STALL is a
 * protocol stall: the next SETUP packet ends it (see ControllerEvents::setupReceived()), and so does the next
 * startInTransfer() or armOutTransfer() on EP0.
 *
 * Both setEndpointStall(true) and setEndpointStall(false) drop the transfer state of the endpoint: an IN packet of
 * startInTransfer() that the host has not acknowledged, and the arm of armOutTransfer(). No event is reported for
 * them. After setEndpointStall(false) the endpoint NAKs until the next startInTransfer() or armOutTransfer(). (On the
 * STM32 FS peripheral, STALL, NAK and VALID share one STAT field, so a port cannot keep them.) A packet that the
 * host acknowledged before the call is not dropped: its event is still reported.
 *
 * Calls come from the same context as the events (normally the main loop). An implementation that runs the
 * hardware in an interrupt protects its shared state itself.
 */
class DeviceController {
   public:
   virtual ~DeviceController() = default;

   /// Sets the receiver of the events. usb::Device calls this in its constructor; nullptr removes it.
   virtual void setEventHandler(ControllerEvents *handler) = 0;

   /// Turns the D+ pull-up on, so that the host sees the device.
   virtual void connect() = 0;

   /// Turns the D+ pull-up off. The host sees a disconnect.
   virtual void disconnect() = 0;

   /**
    * @brief Sets the device address (0 to 127) that SET_ADDRESS requests.
    *
    * usb::Device calls this when it accepts SET_ADDRESS, before it starts the status stage. USB 2.0 section 9.4.6
    * requires the old address until the status stage completes, and the host uses the new address 2 ms later
    * (section 9.2.6.3). The events reach usb::Device later than that when they are queued, so the controller
    * applies the address itself:
    * - A peripheral that matches tokens against the address register (STM32 FS: DADDR) writes the register when the
    *   host acknowledges the zero-length status packet on EP0, in the handler of that IN completion.
    * - A peripheral that needs the address before the status stage (for example Synopsys OTG: DCFG) writes it at
    *   once.
    *
    * A controller of the first kind drops a pending address on the next SETUP packet and on a bus reset: the host
    * aborted the SET_ADDRESS. usb::Device changes its own address and state when it receives
    * ControllerEvents::inPacketSent() for the status stage.
    */
   virtual void setAddress(uint8_t address) = 0;

   /**
    * @brief Opens an endpoint. It answers with NAK until it is armed.
    *
    * @return false if the endpoint cannot be opened, for example because the packet memory is full, the number is
    * out of range or the type is not supported.
    */
   virtual bool openEndpoint(const EndpointConfig &config) = 0;

   /// Closes an endpoint. The hardware then ignores tokens for it. Closing a closed endpoint has no effect.
   virtual void closeEndpoint(uint8_t endpointAddress) = 0;

   /// Sets or clears the STALL condition of an endpoint (see "Endpoint halt" above).
   virtual void setEndpointStall(uint8_t endpointAddress, bool stall) = 0;

   /**
    * @brief Sends one packet on an IN endpoint.
    *
    * The controller copies `packet` before it returns, so the caller may reuse the memory. An empty packet sends a
    * zero-length packet. When the host has acknowledged the packet, the controller reports
    * ControllerEvents::inPacketSent(). The caller sends the next packet only after that event.
    *
    * @param packet At most the maximum packet size of the endpoint.
    * @return false if the endpoint is not open, still busy or the packet is too large. Nothing is sent then.
    */
   virtual bool startInTransfer(uint8_t endpointAddress, art::span<const uint8_t> packet) = 0;

   /**
    * @brief Takes back the IN packet of the last startInTransfer() if the host has not acknowledged it.
    *
    * The endpoint then NAKs until the next startInTransfer(). The data toggle does not change. No event is reported
    * for a packet that is taken back. A packet that the host acknowledged before the call is not taken back: its
    * event is still reported, as for setEndpointStall(). Limit: if the host received the packet but its ACK was lost,
    * the host discards the next packet as a repeat. The call has no effect on a stalled endpoint.
    *
    * Use it for a packet that is no longer valid and that the host may never read, for example the reply of a
    * protocol to a host that has gone on with the next command.
    *
    * @return true if no ControllerEvents::inPacketSent() is to come for the endpoint: the packet was taken back, or
    * no packet was pending. false if the host acknowledged the packet and its event is not yet reported. The caller
    * then waits for the event before the next startInTransfer().
    */
   virtual bool cancelInTransfer(uint8_t endpointAddress) = 0;

   /**
    * @brief Lets an OUT endpoint accept one packet. The controller reports it with
    * ControllerEvents::outPacketReceived().
    */
   virtual void armOutTransfer(uint8_t endpointAddress) = 0;

   protected:
   DeviceController() = default;
   DeviceController(const DeviceController &) = default;
   DeviceController &operator=(const DeviceController &) = default;
};

} // namespace art::usb
