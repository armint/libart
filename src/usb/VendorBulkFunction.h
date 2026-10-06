#pragma once

#include "usb/Function.h"
#include "usb/UsbTypes.h"
#include "utils/compat/span.h"
#include <cstddef>
#include <cstdint>

namespace art::usb {

/// The endpoints of a VendorBulkFunction. They must match the endpoint descriptors of its interface.
struct VendorBulkEndpoints {
   /// The bulk OUT endpoint (bit 7 clear).
   uint8_t out{ 0x01u };
   /// The bulk IN endpoint (bit 7 set).
   uint8_t in{ 0x81u };
   /// wMaxPacketSize of both endpoints: 8, 16, 32 or 64 at full speed.
   uint16_t maxPacketSize{ 64u };
};

/**
 * @brief Base of a function with one interface, one bulk OUT and one bulk IN endpoint, for a protocol of its own
 * (normally a vendor-specific class, bInterfaceClass 0xFF, which a host reaches with libusb or WinUSB).
 *
 * The base runs the data path. A subclass implements the protocol in the protected hooks: it gets each OUT packet in
 * dataReceived() and sends its replies with sendInTransfer(). The descriptors are not part of the function; build
 * them with usb/DescriptorBuilder.h (interfaceDescriptor() with classcode::kVendorSpecific and two bulk endpoints).
 *
 * @par OUT path
 * The OUT endpoint is armed after the configuration. Each packet goes to dataReceived(). If the hook returns true,
 * the endpoint is armed again at once. If it returns false, the endpoint is held: it NAKs and the host waits, until
 * the subclass calls resumeOut(). So a subclass that processes a command later, in the main loop, loses no packet.
 * The base does not join packets into transfers: the protocol knows where a command ends.
 *
 * @par IN path
 * sendInTransfer() sends a buffer of any size in packets of maxPacketSize bytes. With ZeroLengthPacket::Auto, a
 * zero-length packet (ZLP) follows when the size is a multiple of maxPacketSize, so that a host that reads more than
 * the transfer sees its end. With ZeroLengthPacket::Never no ZLP follows: use it when the host reads exactly the
 * size of the transfer, because the ZLP would end its next read. An empty buffer sends one ZLP in both modes. The
 * buffer is not copied: it must stay valid and unchanged until transferSent() or dataPathClosed().
 *
 * @par Endpoint halt
 * The function sends and arms nothing on a halted endpoint. A subclass halts an endpoint with
 * Device::haltEndpoint() (device()), for example on a protocol error. When the host clears the halt, or
 * SET_INTERFACE reopens the interface, the controller has dropped the transfer of the endpoint (see
 * DeviceController, "Endpoint halt"). The function then arms the OUT endpoint again (unless it is held) and sends
 * the dropped IN packet again: the transfer continues where it stopped, so no byte is lost or sent twice.
 *
 * @par Configuration changes
 * configured() opens the data path and calls dataPathOpened(). deconfigured() (bus reset, SET_CONFIGURATION 0,
 * Device::stop()) drops the IN transfer, releases the hold of the OUT endpoint and calls dataPathClosed(). A suspend
 * changes nothing.
 *
 * @par Threading
 * All calls, sendInTransfer() and resumeOut() included, must come from the context that delivers the controller
 * events (normally the main loop). The hooks run in that context too. The function does no locking.
 */
class VendorBulkFunction : public Function {
   public:
   /// The end of an IN transfer whose size is a multiple of maxPacketSize (see "IN path").
   enum class ZeroLengthPacket : uint8_t {
      /// A ZLP follows a transfer whose size is a multiple of maxPacketSize.
      Auto,
      /// No ZLP follows (except for an empty transfer).
      Never,
   };

   /**
    * @brief Starts an IN transfer.
    *
    * @param data The data, of any size. It is not copied: keep it valid and unchanged until transferSent() or
    * dataPathClosed().
    * @param zeroLengthPacket See "IN path".
    * @return false if the function is not configured, the last transfer is still on its way, or the controller
    * refused the first packet (the endpoint is not open, or VendorBulkEndpoints::maxPacketSize is larger than the
    * wMaxPacketSize of the descriptor). Nothing is sent then. On a halted IN endpoint the transfer starts (true) and
    * waits until the halt is cleared.
    */
   bool sendInTransfer(art::span<const uint8_t> data, ZeroLengthPacket zeroLengthPacket = ZeroLengthPacket::Auto);

   /// True from sendInTransfer() until the host acknowledged the last packet of the transfer (the ZLP included).
   [[nodiscard]] bool isInTransferBusy() const {
      return inTransferBusy_;
   }

   /**
    * @brief Arms the OUT endpoint again after dataReceived() returned false.
    *
    * It does nothing if the endpoint is not held, or the function is not configured.
    */
   void resumeOut();

   /// True after dataReceived() returned false, until resumeOut() or deconfigured().
   [[nodiscard]] bool isOutHeld() const {
      return outHeld_;
   }

   /// True between configured() and deconfigured().
   [[nodiscard]] bool isConfigured() const {
      return configured_;
   }

   [[nodiscard]] const VendorBulkEndpoints &endpoints() const {
      return endpoints_;
   }

   // Function. A subclass that overrides controlRequest() (for vendor requests) or the suspend hooks may do so; the
   // data path hooks are final, use the protected hooks below instead.
   void configured() final;
   void deconfigured() final;
   void alternateSettingChanged(uint8_t interfaceNumber, uint8_t alternateSetting) final;
   void outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) final;
   void inPacketSent(uint8_t endpointAddress) final;
   void endpointHaltCleared(uint8_t endpointAddress) final;

   protected:
   /**
    * @param interfaceNumber The bInterfaceNumber of the interface.
    * @param endpoints The endpoints of the interface.
    */
   VendorBulkFunction(uint8_t interfaceNumber, const VendorBulkEndpoints &endpoints);

   /**
    * @brief An OUT packet arrived.
    *
    * @param packet The packet, at most maxPacketSize bytes; empty for a ZLP. Valid only during the call: copy what
    * is needed later.
    * @return true to accept the next packet at once. false holds the OUT endpoint (it NAKs) until resumeOut().
    */
   virtual bool dataReceived(art::span<const uint8_t> packet) = 0;

   /// The host acknowledged the last packet of the transfer of sendInTransfer(). The next transfer can start here.
   virtual void transferSent() {
   }

   /// SET_CONFIGURATION: the data path is open and the OUT endpoint is armed.
   virtual void dataPathOpened() {
   }

   /// The data path is closed (see "Configuration changes"). An IN transfer on its way is dropped: its buffer is
   /// free, and no transferSent() follows.
   virtual void dataPathClosed() {
   }

   private:
   /// Arms the OUT endpoint if it is not armed, not held and not halted.
   void armOut();
   /// Sends the next packet of the IN transfer, or its ZLP, if the IN endpoint is free.
   void sendNextInPacket();
   /// Arms the OUT endpoint again after the controller dropped its arm.
   void restartOut();
   /// Sends the IN packet again that the controller dropped.
   void restartIn();
   /// Forgets the endpoint state and the IN transfer.
   void resetDataPath();

   VendorBulkEndpoints endpoints_;
   bool configured_{ false };
   /// The OUT endpoint is armed.
   bool outArmed_{ false };
   /// dataReceived() returned false: the OUT endpoint stays unarmed until resumeOut().
   bool outHeld_{ false };

   /// The data of the IN transfer. The bytes before inSent_ are acknowledged.
   art::span<const uint8_t> inData_{};
   size_t inSent_{ 0u };
   /// The size of the packet that waits for its acknowledgement.
   size_t inPacketSize_{ 0u };
   /// A transfer is in progress.
   bool inTransferBusy_{ false };
   /// A packet of the transfer waits for its acknowledgement.
   bool inPacketBusy_{ false };
   /// A ZLP must still follow the data of the transfer.
   bool inZlpPending_{ false };
};

} // namespace art::usb
