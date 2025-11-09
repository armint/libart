#pragma once

#include "ModbusCodes.h"
#include "streams/DigestReadable.h"
#include "streams/DigestWritable.h"
#include "streams/Readable.h"
#include "streams/Writable.h"
#include "utils/IBMCRC16Digest.h"
#include "utils/endian.h"

#include <cstdint>
#include <expected>
#include <functional>
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
   std::span<uint16_t> buffer_;

   public:
   using ReadRegisterCallback_t = std::function<bool(uint16_t address, uint16_t count, Writable& writable)>;
   using WriteRegsCb = std::function<bool(uint16_t address, std::span<const uint16_t> values)>;
   using enabler_t = std::function<void(bool)>;


   protected:
   ReadRegisterCallback_t readHolding_{};
   ReadRegisterCallback_t readInput_{};
   WriteRegsCb writeHolding_{};

   enabler_t txEnabler_;
   enabler_t rxEnabler_;

   void enableTx(bool enable) const {
      if(txEnabler_) {
         txEnabler_(enable);
      }
   }
   void enableRx(bool enable) const {
      if(rxEnabler_) {
         rxEnabler_(enable);
      }
   }
   // helpers
   std::expected<size_t, Writable::Error> writeException(uint8_t function, uint8_t exceptionCode);
   bool readRegisters(CRCReadable& r, uint8_t function, bool isBroadcast);
   bool writeMultipleRegisters(CRCReadable& r, uint8_t function, bool isBroadcast);

   public:
   ModbusSlave(Readable &readable, Writable &writable, std::span<uint16_t> buffer, enabler_t rxEnabler = {}, enabler_t txEnabler = {});

   void setAddress(uint8_t addr) {
      address_ = addr;
   }
   uint8_t getAddress() const {
      return address_;
   }

   void setInterCharTimeout(uint32_t t) {
      readable_.setTimeout(t);
   }


   void setReadHoldingCallback(ReadRegisterCallback_t cb) {
      readHolding_ = std::move(cb);
   }
   void setReadInputCallback(ReadRegisterCallback_t cb) {
      readInput_ = std::move(cb);
   }
   void setWriteHoldingCallback(WriteRegsCb cb) {
      writeHolding_ = std::move(cb);
   }

   // Process a single request frame if available. Returns true if a request was processed (regardless of reply), false on timeout/no frame.
   bool poll();
};

} // namespace modbus
