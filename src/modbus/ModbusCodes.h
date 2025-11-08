#pragma once
#include <cstdint>

namespace modbus {
enum FunctionCode : std::uint8_t {
   ReadCoils = 1,
   ReadDiscreteInputs = 2,
   ReadHoldingRegisters = 3,
   ReadInputRegisters = 4,
   WriteSingleCoil = 5,
   WriteSingleRegister = 6,
   ReadExceptionStatus = 7,
   GetComEventCounter = 11,
   GetComEventLog = 12,
   WriteMultipleCoils = 15,
   WriteMultipleRegisters = 16,
   ReportServerID = 17,
   ReadFileRecord = 20,
   WriteFileRecord = 21,
   MaskWriteRegister = 22,
   ReadWriteMultipleRegisters = 23,
   ReadFIFOQueue = 24,
};

enum ResultCode : uint8_t {
   OK = 0,
   ILLEGAL_FUNCTION = 1,
   ILLEGAL_ADDRESS = 2,
   ILLEGAL_VALUE = 3,
   FAILURE = 4,
   ACK = 5,
   BUSY = 6,
   PARITY_ERROR = 8,
   PATH_UNAVAILABLE = 10,
   NO_TARGET_DEVICE_RESPONSE = 11,
};
} // namespace modbus
