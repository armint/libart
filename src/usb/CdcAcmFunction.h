#pragma once

#include "usb/Function.h"
#include "usb/UsbTypes.h"
#include "utils/compat/span.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <etl/circular_buffer.h>
#include <optional>

namespace art::usb {

/**
 * @brief The line coding of a CDC-ACM function (PSTN 1.20 section 6.3.11, table 17).
 *
 * The fields keep the names of the specification. The function only stores the values: a virtual COM port has no
 * UART behind it.
 */
struct LineCoding {
   /// Size of the line coding structure on the wire.
   static constexpr size_t kSize = 7u;

   /// Data terminal rate in bits per second.
   uint32_t dwDTERate{ 115200u };
   /// Stop bits: 0 = 1, 1 = 1.5, 2 = 2.
   uint8_t bCharFormat{ 0u };
   /// Parity: 0 = none, 1 = odd, 2 = even, 3 = mark, 4 = space.
   uint8_t bParityType{ 0u };
   /// Data bits: 5, 6, 7, 8 or 16.
   uint8_t bDataBits{ 8u };
};

/// The endpoints of a CDC-ACM function. They must match the endpoint descriptors of its two interfaces.
struct CdcAcmEndpoints {
   /// The interrupt IN endpoint of the communication interface (bit 7 set). It carries the SERIAL_STATE
   /// notification.
   uint8_t notification{ 0x81u };
   /// wMaxPacketSize of the notification endpoint. A notification longer than this goes out in several packets.
   uint16_t notificationMaxPacketSize{ 16u };
   /// The bulk OUT endpoint of the data interface (bit 7 clear). Its wMaxPacketSize must be kDataPacketSize.
   uint8_t dataOut{ 0x02u };
   /// The bulk IN endpoint of the data interface (bit 7 set). Its wMaxPacketSize must be kDataPacketSize.
   uint8_t dataIn{ 0x82u };
};

/**
 * @brief The CDC-ACM function (virtual COM port) without its queues. Use CdcAcmFunction, which owns them.
 *
 * The function owns two interfaces: the communication interface `controlInterface` (with the notification
 * endpoint) and the data interface `controlInterface + 1` (with the bulk endpoints). The descriptors are not part
 * of the function; build them with usb/DescriptorBuilder.h (an IAD, cdcHeader(), cdcCallManagement(), cdcAcm() and
 * cdcUnion()).
 *
 * @par Class requests
 * - SET_LINE_CODING and GET_LINE_CODING: the function stores the line coding and returns it. It does not use it.
 * - SET_CONTROL_LINE_STATE: DTR set means that a terminal is open on the host (isTerminalOpen()).
 * - Every other request stalls, SEND_BREAK too (cdcAcm() does not declare it).
 *
 * @par Data path
 * The bulk endpoints move data between the host and two fixed-size queues. write() puts data into the TX queue and
 * the function sends it in packets of kDataPacketSize bytes. If the last packet of the queue is a full packet, a
 * zero-length packet (ZLP) follows, so that the host sees the end of the transfer. read() takes data from the RX
 * queue.
 *
 * @par Flow control
 * The OUT endpoint is armed only while the RX queue has at least kDataPacketSize free bytes. Otherwise it NAKs and
 * the host waits, until read() makes room. So no received byte is ever dropped.
 *
 * @par Configuration changes
 * deconfigured() (bus reset, SET_CONFIGURATION 0, Device::stop()) clears both queues and the control line state.
 * The line coding stays. A suspend changes nothing: the IN packet that is on its way stays pending.
 *
 * @par Endpoint halt
 * The function sends and arms nothing on a halted endpoint. When the host clears the halt of a data endpoint, or
 * SET_INTERFACE reopens the data interface, the controller has dropped the transfer of the endpoint (see
 * DeviceController, "Endpoint halt"). The function then arms the OUT endpoint again and sends the dropped IN packet
 * again: the bytes of an IN packet stay in the TX queue until the host acknowledges it, so no byte is lost or sent
 * twice. A notification on its way is dropped.
 *
 * @par Threading
 * All calls, read() and write() included, must come from the context that delivers the controller events
 * (normally the main loop). The function does no locking.
 */
class CdcAcmFunctionBase : public Function {
   public:
   /// wMaxPacketSize of the bulk endpoints (the maximum for full speed).
   static constexpr uint16_t kDataPacketSize = 64u;

   /// Class request codes (CDC 1.20 table 19, PSTN 1.20 table 13).
   static constexpr uint8_t kSetLineCoding = 0x20u;
   static constexpr uint8_t kGetLineCoding = 0x21u;
   static constexpr uint8_t kSetControlLineState = 0x22u;

   /// wValue bits of SET_CONTROL_LINE_STATE (PSTN 1.20 table 18).
   static constexpr uint16_t kControlLineDtr = 0x0001u;
   static constexpr uint16_t kControlLineRts = 0x0002u;

   /// bNotification code of SERIAL_STATE (PSTN 1.20 table 30).
   static constexpr uint8_t kSerialStateNotification = 0x20u;
   /// Size of the SERIAL_STATE notification: an 8-byte header and the 2-byte UART state bitmap.
   static constexpr size_t kSerialStateNotificationSize = 10u;

   /// Bits of the UART state bitmap of SERIAL_STATE (PSTN 1.20 table 31).
   static constexpr uint16_t kSerialStateDcd = 0x0001u;
   static constexpr uint16_t kSerialStateDsr = 0x0002u;
   static constexpr uint16_t kSerialStateBreak = 0x0004u;
   static constexpr uint16_t kSerialStateRing = 0x0008u;
   static constexpr uint16_t kSerialStateFramingError = 0x0010u;
   static constexpr uint16_t kSerialStateParityError = 0x0020u;
   static constexpr uint16_t kSerialStateOverrun = 0x0040u;

   /**
    * @brief Takes received data out of the RX queue.
    *
    * If this makes kDataPacketSize bytes free, the OUT endpoint is armed again.
    *
    * @return The number of bytes copied into `buffer`; 0 if the RX queue is empty.
    */
   size_t read(art::span<uint8_t> buffer);

   /**
    * @brief Puts data into the TX queue and starts to send it.
    *
    * It never waits: the data that does not fit is not taken.
    *
    * @return The number of bytes taken from `data`; 0 if the function is not configured or the TX queue is full.
    * The packet on its way still takes its room in the TX queue.
    */
   size_t write(art::span<const uint8_t> data);

   /// The number of received bytes in the RX queue.
   [[nodiscard]] size_t readAvailable() const {
      return rxQueue_.size();
   }

   /// The number of free bytes in the TX queue (0 if the function is not configured).
   [[nodiscard]] size_t writeAvailable() const {
      return configured_ ? txQueue_.available() : 0u;
   }

   /// True if the TX queue is empty and the host acknowledged the last packet (the ZLP included).
   [[nodiscard]] bool isTxIdle() const {
      return txQueue_.empty() && !txBusy_ && !zlpPending_;
   }

   /// True between configured() and deconfigured().
   [[nodiscard]] bool isConfigured() const {
      return configured_;
   }

   /// True if the host set DTR: a terminal program has the port open.
   [[nodiscard]] bool isTerminalOpen() const {
      return (controlLineState_ & kControlLineDtr) != 0u;
   }

   /// The wValue of the last SET_CONTROL_LINE_STATE (kControlLineDtr, kControlLineRts); 0 after deconfigured().
   [[nodiscard]] uint16_t controlLineState() const {
      return controlLineState_;
   }

   /// The line coding of the last SET_LINE_CODING, or the default 115200 8N1.
   [[nodiscard]] const LineCoding &lineCoding() const {
      return lineCoding_;
   }

   /// The bInterfaceNumber of the data interface.
   [[nodiscard]] uint8_t dataInterface() const {
      return static_cast<uint8_t>(firstInterface() + 1u);
   }

   /**
    * @brief Sends the SERIAL_STATE notification with a UART state bitmap (kSerialState*).
    *
    * The notification is optional: a host does not need it for a virtual COM port.
    *
    * @return false if the function is not configured, or the last notification is still on its way. Nothing is
    * sent then.
    */
   bool notifySerialState(uint16_t serialState);

   // Function
   std::optional<uint16_t> controlRequest(const SetupPacket &setup, art::span<uint8_t> data) override;
   void configured() override;
   void deconfigured() override;
   void alternateSettingChanged(uint8_t interfaceNumber, uint8_t alternateSetting) override;
   void outPacketReceived(uint8_t endpointAddress, art::span<const uint8_t> packet) override;
   void inPacketSent(uint8_t endpointAddress) override;
   void endpointHaltCleared(uint8_t endpointAddress) override;

   protected:
   /**
    * @param controlInterface The bInterfaceNumber of the communication interface. The data interface is the next.
    * @param endpoints The endpoints of the two interfaces.
    * @param rxQueue The RX queue, with a capacity of at least kDataPacketSize.
    * @param txQueue The TX queue.
    */
   CdcAcmFunctionBase(uint8_t controlInterface,
                      const CdcAcmEndpoints &endpoints,
                      etl::icircular_buffer<uint8_t> &rxQueue,
                      etl::icircular_buffer<uint8_t> &txQueue);

   private:
   /// Arms the OUT endpoint if it is not armed and the RX queue has room for a full packet.
   void armOutIfRoom();
   /// Sends the next packet of the TX queue, or the ZLP, if the IN endpoint is free.
   void sendNextInPacket();
   /// Sends the next packet of the notification.
   void sendNextNotificationPacket();
   /// Forgets the endpoint state of the data interface and the pending ZLP (configuration change).
   void resetDataEndpoints();
   /// Arms the OUT endpoint again after the controller dropped its arm.
   void restartOut();
   /// Sends the IN packet again that the controller dropped, or the next one.
   void restartIn();
   /// Forgets the notification on its way.
   void dropNotification();

   CdcAcmEndpoints endpoints_;
   etl::icircular_buffer<uint8_t> &rxQueue_;
   etl::icircular_buffer<uint8_t> &txQueue_;

   LineCoding lineCoding_{};
   uint16_t controlLineState_{ 0u };
   bool configured_{ false };
   /// The OUT endpoint is armed.
   bool outArmed_{ false };
   /// An IN data packet waits for its acknowledgement.
   bool txBusy_{ false };
   /// The size of that packet. Its bytes are still at the front of the TX queue.
   size_t txInFlight_{ 0u };
   /// The last acknowledged data packet was a full packet: a ZLP must follow if the TX queue is empty.
   bool zlpPending_{ false };

   std::array<uint8_t, kSerialStateNotificationSize> notification_{};
   /// Bytes of notification_ already sent; kSerialStateNotificationSize if no notification is in progress.
   size_t notificationSent_{ kSerialStateNotificationSize };
   /// A notification packet waits for its acknowledgement.
   bool notificationBusy_{ false };
};

/**
 * @brief The CDC-ACM function with its RX and TX queues (see CdcAcmFunctionBase).
 *
 * @tparam RxSize Capacity of the RX queue in bytes, at least CdcAcmFunctionBase::kDataPacketSize. With exactly
 * kDataPacketSize, the OUT endpoint accepts the next packet only when the queue is empty.
 * @tparam TxSize Capacity of the TX queue in bytes.
 *
 * Register it with Device::addFunction(). Code that only reads and writes can take a CdcAcmFunctionBase &, so it
 * does not depend on the queue sizes.
 */
template <size_t RxSize, size_t TxSize>
class CdcAcmFunction final : public CdcAcmFunctionBase {
   static_assert(RxSize >= kDataPacketSize, "the RX queue must hold at least one full packet");
   static_assert(TxSize > 0u, "the TX queue must not be empty");

   public:
   CdcAcmFunction(uint8_t controlInterface, const CdcAcmEndpoints &endpoints)
      : CdcAcmFunctionBase(controlInterface, endpoints, rxBuffer_, txBuffer_) {
   }

   private:
   // The base class is constructed before these members. Its constructor only stores the references and does not
   // use them.
   etl::circular_buffer<uint8_t, RxSize> rxBuffer_;
   etl::circular_buffer<uint8_t, TxSize> txBuffer_;
};

} // namespace art::usb
