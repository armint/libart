#pragma once

#include "ModbusCodes.h"
#include "streams/DigestWritable.h"
#include "streams/Writable.h"
#include "utils/IBMCRC16FastDigest.h"
#include "utils/systick.h"

#include <streams/DigestReadable.h>

#include <cstdint>

namespace modbus {

enum class ResponseStatus : uint8_t {
   ReadTimeout,
   WriteError,
   AddressMismatch,
   FunctionCodeMismatch,
   FunctionCodeException,
   CRCMismatch,
   BufferOverflow
};

constexpr const char *responseStatusToString(ResponseStatus r) {
   constexpr const char *kResponseStatusStrings[] = {
      "Read Timeout",            // 0 ReadTimeout
      "Write Error",             // 1 WriteError
      "Address Mismatch",        // 2 AddressMismatch
      "Function Code Mismatch",  // 3 FunctionCodeMismatch
      "Function Code Exception", // 4 FunctionCodeException
      "CRC Mismatch",            // 5 CRCMismatch
      "Buffer too small"         // 7 BufferOverflow
   };
   const auto idx = static_cast<uint8_t>(r);
   return idx < std::size(kResponseStatusStrings) ? kResponseStatusStrings[idx] : "Not a value";
}


class ModbusMaster {
   public:
   using enabler_t = void(*)(bool);

   protected:
   class CRCReadable : public DigestReadable {
      public:
      using Readable::read;

      IBMCRC16FastDigest digest_;

      explicit CRCReadable(Readable &source)
         : DigestReadable(source, digest_) {}

      bool readCRCAndValidate();
   };

   class CRCWritable : public DigestWritable {
      public:
      IBMCRC16FastDigest digest_;

      explicit CRCWritable(Writable &sink)
         : DigestWritable(sink, digest_) {}

      std::expected<size_t, Error> writeCRC();
   };
   Readable &readable_;
   Writable &writable_;
   duration_t _responseTimeout = 3000;
   duration_t symbolTimeout_ = 20;
   uint8_t _exceptionCode = 0;

   std::expected<uint8_t, ResponseStatus> parseHeader(uint8_t deviceAddress, Readable &r, uint8_t expectedFunctionCode);
   std::expected<uint8_t, ResponseStatus> parseResponse(uint8_t deviceAddress, std::span<uint8_t> buffer, uint8_t functionCode);
   std::expected<uint8_t, ResponseStatus>
   readRegisters(uint8_t deviceAddress, FunctionCode code, uint16_t address, std::span<uint8_t> buffer);

   enabler_t txEnabler = {};
   enabler_t rxEnabler = {};
   void enableTx() const {
      if(txEnabler) {
         txEnabler(true);
      }
   }
   void enableRx() const {
      if(rxEnabler) {
         rxEnabler(true);
      }
   }

   public:
   ModbusMaster(Readable &readable, Writable &writable);

   duration_t getResponseTimeout() const {
      return _responseTimeout;
   }

   void setResponseTimeout(uint32_t timeout) {
      _responseTimeout = timeout;
   }

   std::expected<uint8_t, ResponseStatus> readInputRegisters(uint8_t deviceAddress, uint16_t startAddress, std::span<uint8_t> buffer) {
      return readRegisters(deviceAddress, FunctionCode::ReadInputRegisters, startAddress, buffer);
   }
   std::expected<uint8_t, ResponseStatus>
   readHoldingRegisters(uint8_t deviceAddress, uint16_t startAddress, std::span<uint8_t> buffer) {
      return readRegisters(deviceAddress, FunctionCode::ReadHoldingRegisters, startAddress, buffer);
   }
   std::expected<uint8_t, ResponseStatus>
   writeMultipleRegisters(uint8_t deviceAddress, uint16_t startAddress, std::span<const uint8_t> values);
   std::expected<uint8_t, ResponseStatus> writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress, uint16_t value);

   [[nodiscard]] uint8_t getExceptionCode() const {
      return _exceptionCode;
   }

   void set_tx_enabler(const enabler_t &tx_enabler) {
      txEnabler = tx_enabler;
   }
   void set_rx_enabler(const enabler_t &rx_enabler) {
      rxEnabler = rx_enabler;
   }
};

} // namespace modbus
