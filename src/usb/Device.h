#pragma once

#include "usb/DeviceController.h"
#include "usb/Function.h"
#include "usb/UsbTypes.h"
#include "usb/VendorRequestHandler.h"
#include "utils/compat/span.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace art::usb {

/**
 * @brief The descriptors that usb::Device serves for GET_DESCRIPTOR.
 *
 * All spans reference memory that must stay valid for the lifetime of the device (normally `constexpr` arrays in
 * flash). usb::Device reads the configuration descriptor to find the interfaces, alternate settings and endpoints,
 * so it must be well-formed: bLength of every descriptor at least 2, and the whole set exactly
 * configuration.size() bytes long (wTotalLength).
 *
 * Each field has a default member initializer, so that an aggregate initialization that leaves out a field (for
 * example the optional bos) does not trigger -Wmissing-field-initializers.
 */
struct Descriptors {
   /// The 18-byte device descriptor. bMaxPacketSize0 sets the packet size of EP0.
   art::span<const uint8_t> device{};
   /// The full configuration descriptor set (configuration, interface, endpoint and class descriptors) of the
   /// single configuration of the device.
   art::span<const uint8_t> configuration{};
   /// The string descriptors by index. Index 0 is the LANGID table. Empty if the device has no strings.
   art::span<const art::span<const uint8_t>> strings{};
   /// The BOS descriptor with its device capability descriptors (GET_DESCRIPTOR(BOS), index 0). Empty if the device
   /// has none: then GET_DESCRIPTOR(BOS) stalls. A device with a BOS sets bcdUSB to 0x0201 or more, so that the host
   /// asks for it.
   art::span<const uint8_t> bos{};
};

/**
 * @brief USB device protocol layer: the chapter 9 state machine, the standard requests on endpoint 0 and the
 * routing to the functions of the device.
 *
 * The device runs on a DeviceController and receives its events (it registers itself as the event handler in the
 * constructor). It needs no heap: the EP0 buffer and the per-interface and per-endpoint state are members.
 *
 * @par States
 * Detached → start() → Powered → bus reset → Default → SET_ADDRESS → Address → SET_CONFIGURATION → Configured.
 * A bus reset returns to Default from every state but Detached. Suspend is entered from every connected state, and
 * resume returns to the state before.
 *
 * @par Standard requests
 * GET_DESCRIPTOR (device, configuration, string, BOS if Descriptors::bos is set; device qualifier and other-speed
 * configuration stall, because a full-speed-only device has none), SET_ADDRESS (applied after the status stage),
 * GET_CONFIGURATION, SET_CONFIGURATION, GET_STATUS, CLEAR_FEATURE and SET_FEATURE (endpoint halt; device remote wakeup
 * only if the configuration descriptor declares it), GET_INTERFACE and SET_INTERFACE. Everything else stalls:
 * SET_DESCRIPTOR, SYNCH_FRAME, TEST_MODE, requests in a state where USB 2.0 does not define them, unknown interfaces
 * and endpoints. A standard request with an interface recipient that the device does not handle itself (for example a
 * class descriptor) goes to the function of the interface.
 *
 * @par Control transfers
 * Replies longer than one packet are split into packets of bMaxPacketSize0. If a reply is shorter than wLength and
 * a multiple of the packet size, a zero-length packet ends the data stage. Host-to-device data stages are collected
 * in a buffer of kControlBufferSize bytes; a request with a longer data stage stalls.
 *
 * @par Class requests
 * Class and vendor requests whose recipient is an interface, or an endpoint of an interface, go to the Function that
 * owns the interface. Vendor requests whose recipient is the device go to the VendorRequestHandler of
 * setVendorRequestHandler(), in the states Address and Configured. Other class and vendor requests stall.
 */
class Device final : public ControllerEvents {
   public:
   /// Maximum number of functions (addFunction()).
   static constexpr size_t kMaxFunctions = 4u;
   /// Maximum number of interfaces of the configuration (bNumInterfaces).
   static constexpr size_t kMaxInterfaces = 8u;
   /// Size of the buffer for host-to-device data stages and function replies.
   static constexpr size_t kControlBufferSize = 64u;

   /**
    * @brief Creates the device in the state Detached and registers it as the event handler of the controller.
    *
    * The controller and the descriptors are referenced, not copied.
    */
   Device(DeviceController &controller, const Descriptors &descriptors);
   ~Device() override;

   Device(const Device &) = delete;
   Device &operator=(const Device &) = delete;

   /**
    * @brief Registers a function. Call it before start().
    *
    * @return false if kMaxFunctions functions are registered, the interface range is empty, reaches beyond
    * kMaxInterfaces or overlaps the range of another function, or the function is registered with a device.
    */
   bool addFunction(Function &function);

   /**
    * @brief Registers the handler of the vendor requests whose recipient is the device. Call it before start().
    *
    * The handler is referenced, not copied. nullptr removes it: then these requests stall.
    */
   void setVendorRequestHandler(VendorRequestHandler *handler) {
      vendorRequestHandler_ = handler;
   }

   /// Connects to the bus (pull-up on). Detached → Powered.
   void start();

   /// Disconnects from the bus (pull-up off), closes all endpoints and tells the functions. Any state → Detached.
   void stop();

   [[nodiscard]] DeviceState state() const {
      return state_;
   }

   /// The device address set by SET_ADDRESS, 0 in the states Detached, Powered and Default.
   [[nodiscard]] uint8_t address() const {
      return address_;
   }

   /// bConfigurationValue of the active configuration, 0 if not configured.
   [[nodiscard]] uint8_t configuration() const {
      return configuration_;
   }

   /// True in the state Configured (also while it is suspended in it).
   [[nodiscard]] bool isConfigured() const {
      return configuration_ != 0u;
   }

   /// The active alternate setting of an interface, 0 for an unknown interface.
   [[nodiscard]] uint8_t alternateSetting(uint8_t interfaceNumber) const;

   /// True if the endpoint is halted by SET_FEATURE(ENDPOINT_HALT) or haltEndpoint().
   [[nodiscard]] bool isEndpointHalted(uint8_t endpointAddress) const;

   /// For functions: sends one packet on an IN endpoint (see DeviceController::startInTransfer()).
   bool startInTransfer(uint8_t endpointAddress, art::span<const uint8_t> packet);

   /// For functions: lets an OUT endpoint accept one packet (see DeviceController::armOutTransfer()).
   void armOutTransfer(uint8_t endpointAddress);

   /**
    * @brief For functions: halts an open endpoint other than EP0, as SET_FEATURE(ENDPOINT_HALT) does.
    *
    * The halt stays until the host clears it with CLEAR_FEATURE(ENDPOINT_HALT), a SET_CONFIGURATION or
    * SET_INTERFACE reopens the endpoint, or a bus reset closes it.
    */
   void haltEndpoint(uint8_t endpointAddress);

   // ControllerEvents
   void busReset() override;
   void suspended() override;
   void resumed() override;
   void setupReceived(const SetupPacket &setup) override;
   void outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) override;
   void inPacketSent(uint8_t endpointAddress) override;

   private:
   /// Stage of the control transfer on EP0.
   enum class ControlStage : uint8_t {
      /// No transfer, or the last one ended (or stalled).
      Idle,
      /// Sending the data stage of a device-to-host request.
      DataIn,
      /// Receiving the data stage of a host-to-device request.
      DataOut,
      /// Waiting for the host to acknowledge the zero-length status packet that we sent.
      StatusIn,
      /// Waiting for the zero-length status packet of the host.
      StatusOut,
   };

   /// Marks "no interface" in endpointInterface_.
   static constexpr uint8_t kNoInterface = 0xFFu;
   /// Endpoint slots: OUT endpoints 0 to 15, then IN endpoints 0 to 15.
   static constexpr size_t kEndpointSlots = 2u * kMaxEndpointNumbers;

   static size_t endpointSlot(uint8_t endpointAddress);

   // Request handling. Each returns false to stall the request.
   bool handleRequest(const SetupPacket &setup);
   bool handleStandardRequest(const SetupPacket &setup);
   bool handleDeviceRequest(const SetupPacket &setup);
   bool handleInterfaceRequest(const SetupPacket &setup);
   bool handleEndpointRequest(const SetupPacket &setup);
   bool handleGetDescriptor(const SetupPacket &setup);
   bool handleSetConfiguration(const SetupPacket &setup);
   bool handleSetInterface(const SetupPacket &setup);
   bool forwardToFunction(const SetupPacket &setup, Function *function);
   bool forwardToVendorRequestHandler(const SetupPacket &setup);

   // Control transfer stages.
   void sendControlData(art::span<const uint8_t> data);
   void sendNextControlPacket();
   void startStatusIn();
   void startStatusOut();
   void stallControl();
   void finishControlOut();

   // Configuration handling.
   bool openInterfaceEndpoints(uint8_t interfaceNumber, uint8_t alternateSetting);
   void closeInterfaceEndpoints(uint8_t interfaceNumber);
   void closeAllEndpoints();
   void deconfigure();
   [[nodiscard]] bool hasAlternateSetting(uint8_t interfaceNumber, uint8_t alternateSetting) const;
   [[nodiscard]] uint8_t configurationAttributes() const;
   [[nodiscard]] uint8_t configurationValue() const;
   [[nodiscard]] uint8_t interfaceCount() const;

   [[nodiscard]] Function *functionForInterface(uint8_t interfaceNumber) const;
   [[nodiscard]] Function *functionForEndpoint(uint8_t endpointAddress) const;
   [[nodiscard]] bool isEndpointOpen(uint8_t endpointAddress) const;

   template <typename Callback>
   void forEachFunction(Callback &&callback) const {
      for(size_t index = 0u; index < functionCount_; ++index) {
         callback(*functions_[index]);
      }
   }

   DeviceController &controller_;
   Descriptors descriptors_;
   uint8_t maxPacketSize0_;

   DeviceState state_{ DeviceState::Detached };
   /// The state before suspended(), restored by resumed().
   DeviceState stateBeforeSuspend_{ DeviceState::Detached };
   uint8_t address_{ 0u };
   uint8_t configuration_{ 0u };
   bool remoteWakeupEnabled_{ false };

   VendorRequestHandler *vendorRequestHandler_{ nullptr };
   std::array<Function *, kMaxFunctions> functions_{};
   size_t functionCount_{ 0u };

   std::array<uint8_t, kMaxInterfaces> alternateSettings_{};
   /// The interface number that owns each open endpoint slot, kNoInterface if closed (EP0 is kNoInterface too).
   std::array<uint8_t, kEndpointSlots> endpointInterface_{};
   /// Bit per endpoint slot: halted.
   uint32_t haltedEndpoints_{ 0u };

   // Control transfer on EP0.
   ControlStage controlStage_{ ControlStage::Idle };
   SetupPacket controlSetup_{};
   /// Remaining data of the data-in stage. Points into the descriptors or controlBuffer_.
   art::span<const uint8_t> controlInData_{};
   /// The data-in stage ends with a zero-length packet after controlInData_ is sent.
   bool controlZlpPending_{ false };
   /// Bytes of the data-out stage received in controlBuffer_.
   uint16_t controlOutReceived_{ 0u };
   /// SET_ADDRESS waits for its status stage to complete.
   bool addressPending_{ false };
   uint8_t pendingAddress_{ 0u };
   std::array<uint8_t, kControlBufferSize> controlBuffer_{};
};

} // namespace art::usb
