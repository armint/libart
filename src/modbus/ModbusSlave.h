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

namespace modbus {

class ModbusSlave {
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
   uint8_t address_ = 1;            // default slave address
   uint32_t requestTimeout_ = 1000; // time to wait for the first byte of a frame
   uint32_t interCharTimeout_ = 20; // time between subsequent bytes in a frame

   public:
   using ReadRegsCb = std::function<bool(uint16_t start, uint16_t count, std::span<uint16_t> out)>;
   using WriteRegsCb = std::function<bool(uint16_t start, std::span<const uint16_t> values)>;

   protected:
   ReadRegsCb readHolding_{};
   ReadRegsCb readInput_{};
   WriteRegsCb writeHolding_{};

   // helpers
   std::expected<void, Writable::Error> writeException(uint8_t function, uint8_t exceptionCode);
   std::expected<void, Writable::Error> writeReadResponse(uint8_t function, std::span<const uint16_t> regs);
   std::expected<void, Writable::Error> writeWriteMultiResponse(uint16_t start, uint16_t count);
   bool readInputRegisters(CRCReadable r, uint8_t function, bool isBroadcast);
   bool writeMultipleRegisters(CRCReadable r, uint8_t function, bool isBroadcast);

   public:
   ModbusSlave(Readable &readable, Writable &writable)
      : readable_(readable)
      , writable_(writable) {}

   void setAddress(uint8_t addr) {
      address_ = addr;
   }
   uint8_t getAddress() const {
      return address_;
   }

   void setRequestTimeout(uint32_t t) {
      requestTimeout_ = t;
   }
   void setInterCharTimeout(uint32_t t) {
      interCharTimeout_ = t;
   }

   void onReadHolding(ReadRegsCb cb) {
      readHolding_ = std::move(cb);
   }
   void onReadInput(ReadRegsCb cb) {
      readInput_ = std::move(cb);
   }
   void onWriteHolding(WriteRegsCb cb) {
      writeHolding_ = std::move(cb);
   }

   // Process a single request frame if available. Returns true if a request was processed (regardless of reply), false on timeout/no frame.
   bool poll();
};

} // namespace modbus
