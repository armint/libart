#pragma once

#include "ModbusCodes.h"
#include "streams/DigestWritable.h"
#include "streams/Writable.h"
#include "utils/IBMCRC16Digest.h"
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
   uint8_t _address = 0;
   duration_t _responseTimeout = 3000;
   duration_t symbolTimeout_ = 20;
   uint8_t _exceptionCode = 0;

   std::expected<uint8_t, ResponseStatus> parseHeader(Readable &r, uint8_t expectedFunctionCode);
   std::expected<uint8_t, ResponseStatus> parseResponse(std::span<uint8_t> buffer, uint8_t functionCode);
   std::expected<uint8_t, ResponseStatus> readRegisters(uint8_t type, uint16_t startAddress, std::span<uint8_t> buffer);

   public:
   void enableTX();
   void enableRX();

   ModbusMaster(Readable &readable, Writable &writable);

   void setAddress(uint8_t address) {
      _address = address;
   }

   uint8_t getAddress() const {
      return _address;
   }

   duration_t getResponseTimeout() const {
      return _responseTimeout;
   }

   void setResponseTimeout(uint32_t timeout) {
      _responseTimeout = timeout;
   }

   std::expected<uint8_t, ResponseStatus> readRegisters(uint16_t startAddress, std::span<uint8_t> buffer);
   std::expected<uint8_t, ResponseStatus> readInputRegisters(uint16_t startAddress, std::span<uint8_t> buffer) {
      return readRegisters(FunctionCode::ReadInputRegisters, startAddress, buffer);
   }
   std::expected<uint8_t, ResponseStatus> readHoldingRegisters(uint16_t startAddress, std::span<uint8_t> buffer) {
      return readRegisters(FunctionCode::ReadHoldingRegisters, startAddress, buffer);
   }
   std::expected<uint8_t, ResponseStatus> writeMultipleRegisters(uint16_t address, std::span<const uint8_t> values);
   std::expected<uint8_t, ResponseStatus> writeSingleRegister(uint16_t address, uint16_t value);

   [[nodiscard]] uint8_t getExceptionCode() const {
      return _exceptionCode;
   }
};

} // namespace modbus
