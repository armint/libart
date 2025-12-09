#pragma once

#include "streams/BoundedReadable.h"
#include "streams/DigestReadable.h"
#include "streams/DigestWritable.h"
#include "streams/Readable.h"
#include "streams/Writable.h"
#include "utils/IBMCRC16Digest.h"

#include <expected>
#include <span>
#include <utility>


namespace modbus {

class ModbusSlave {
   protected:
   class CRCReadable : public DigestReadable {
      public:
      using Readable::read;
      IBMCRC16Digest digest_;
      explicit CRCReadable(Readable &source)
         : DigestReadable(source, digest_) {}
      bool readCRCAndValidate();
   };

   class CRCWritable : public DigestWritable {
      public:
      IBMCRC16Digest digest_;
      explicit CRCWritable(Writable &sink)
         : DigestWritable(sink, digest_) {}
      std::expected<size_t, Error> writeCRC();
   };

   Readable &readable_;
   Writable &writable_;
   uint8_t address_ = 1; // default slave address
   std::span<uint8_t> buffer_;

   public:
   using ReadRegistersCallback_t = bool (*)(void *arg, uint16_t address, uint16_t count, Writable &writable);
   using WriteRegistersCallback_t = bool (*)(void *arg, uint16_t address, BoundedReadable &readable);
   using enabler_t = void (*)(void *arg, bool);


   protected:
   ReadRegistersCallback_t readHolding_{};
   void *read_holding_arg_;
   ReadRegistersCallback_t readInput_{};
   void *read_input_arg_;
   WriteRegistersCallback_t writeHolding_{};
   void *write_holding_arg_;

   enabler_t txEnabler_;
   void *tx_enabler_arg_;
   enabler_t rxEnabler_;
   void *rx_enabler_arg_;

   void enableTx(bool enable) const {
      if(txEnabler_) {
         txEnabler_(tx_enabler_arg_, enable);
      }
   }
   void enableRx(bool enable) const {
      if(rxEnabler_) {
         rxEnabler_(rx_enabler_arg_, enable);
      }
   }
   // helpers
   std::expected<size_t, Writable::Error> writeException(uint8_t function, uint8_t exceptionCode);
   bool readRegisters(CRCReadable &r, uint8_t function, bool isBroadcast);
   bool writeMultipleRegisters(CRCReadable &r, uint8_t function, bool isBroadcast);

   public:
   ModbusSlave(Readable &readable, Writable &writable, std::span<uint8_t> buffer, enabler_t rxEnabler = {}, enabler_t txEnabler = {});

   void setAddress(uint8_t addr) {
      address_ = addr;
   }
   [[nodiscard]] uint8_t getAddress() const {
      return address_;
   }

   void setInterCharTimeout(uint32_t t) {
      readable_.setTimeout(t);
   }


   void setReadHoldingCallback(ReadRegistersCallback_t cb, void *arg) {
      readHolding_ = cb;
      read_holding_arg_ = arg;
   }
   void setReadInputCallback(ReadRegistersCallback_t cb, void *arg) {
      readInput_ = cb;
      read_input_arg_ = arg;
   }
   void setWriteHoldingCallback(WriteRegistersCallback_t cb, void *arg) {
      writeHolding_ = cb;
      write_holding_arg_ = arg;
   }

   // Process a single request frame if available. Returns true if a request was processed (regardless of reply), false on timeout/no frame.
   bool poll();
};

} // namespace modbus
