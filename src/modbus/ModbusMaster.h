#pragma once

#include "ModbusCodes.h"
#include "streams/BufferedReadable.h"
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
   using enabler_t = void (*)(bool);

   protected:
   enum class ReadState : uint8_t { Listening, WaitForIdle, Idle };
   volatile ReadState read_state_ = ReadState::Listening;
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
   BufferedReadable* buffered_readable_ = nullptr;
   Writable &writable_;
   duration_t response_timeout_ = 3000;
   duration_t symbol_timeout_ = 20;
   duration_t packet_timeout_ = 300;
   uint8_t exception_code_ = 0;

   std::expected<uint8_t, ResponseStatus> parseHeader(uint8_t deviceAddress, Readable &r, uint8_t expectedFunctionCode);
   std::expected<uint8_t, ResponseStatus> parseResponse(uint8_t deviceAddress, std::span<uint8_t> buffer, uint8_t functionCode);
   std::expected<uint8_t, ResponseStatus>
   readRegisters(uint8_t deviceAddress, FunctionCode code, uint16_t address, std::span<uint8_t> buffer);
   std::expected<uint8_t, ResponseStatus>
   readBits(uint8_t deviceAddress, FunctionCode code, uint16_t address, uint16_t count, std::span<uint8_t> buffer);

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

   void waitForIdle();

   public:
   ModbusMaster(BufferedReadable &readable, Writable &writable);
   ModbusMaster(Readable &readable, Writable &writable);

   duration_t getResponseTimeout() const {
      return response_timeout_;
   }

   void setResponseTimeout(uint32_t timeout) {
      response_timeout_ = timeout;
   }

   std::expected<uint8_t, ResponseStatus> readInputRegisters(uint8_t deviceAddress, uint16_t startAddress, std::span<uint8_t> buffer) {
      return readRegisters(deviceAddress, FunctionCode::ReadInputRegisters, startAddress, buffer);
   }
   std::expected<uint8_t, ResponseStatus>
   readHoldingRegisters(uint8_t deviceAddress, uint16_t startAddress, std::span<uint8_t> buffer) {
      return readRegisters(deviceAddress, FunctionCode::ReadHoldingRegisters, startAddress, buffer);
   }
   std::expected<uint8_t, ResponseStatus>
   readCoils(uint8_t deviceAddress, uint16_t startAddress, uint16_t count, std::span<uint8_t> buffer) {
      return readBits(deviceAddress, FunctionCode::ReadCoils, startAddress, count, buffer);
   }
   std::expected<uint8_t, ResponseStatus>
   readDiscreteInputs(uint8_t deviceAddress, uint16_t startAddress, uint16_t count, std::span<uint8_t> buffer) {
      return readBits(deviceAddress, FunctionCode::ReadDiscreteInputs, startAddress, count, buffer);
   }
   std::expected<uint8_t, ResponseStatus>
   writeMultipleRegisters(uint8_t deviceAddress, uint16_t startAddress, std::span<const uint8_t> values);
   std::expected<uint8_t, ResponseStatus> writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress, uint16_t value);
   std::expected<uint8_t, ResponseStatus> writeSingleCoil(uint8_t deviceAddress, uint16_t coilAddress, bool value);
   std::expected<uint8_t, ResponseStatus>
   writeMultipleCoils(uint8_t deviceAddress, uint16_t startAddress, uint16_t count, std::span<const uint8_t> values);

   [[nodiscard]] uint8_t getExceptionCode() const {
      return exception_code_;
   }

   void set_tx_enabler(const enabler_t &tx_enabler) {
      txEnabler = tx_enabler;
   }
   void set_rx_enabler(const enabler_t &rx_enabler) {
      rxEnabler = rx_enabler;
   }

   void idleDetected() {
      read_state_ = ReadState::Idle;
   }
};

} // namespace modbus
