#include "ModbusMaster.h"

#include <streams/ArrayReadable.h>
#include <utils/endian.h>

namespace modbus {


ModbusMaster::ModbusMaster(Readable &readable, Writable &writable)
   : readable_(readable)
   , writable_(writable) {
   enableRx();
}
ModbusMaster::ModbusMaster(BufferedReadable &readable, Writable &writable)
   : readable_(readable)
   , writable_(writable)
   , buffered_readable_(&readable) {
   enableRx();
}

bool ModbusMaster::CRCReadable::readCRCAndValidate() {
   uint8_t expected[2];
   digest_.getDigest(std::span(expected, 2));
   uint8_t calculated[2];
   if(auto res = read(calculated); !res) {
      return false;
   }
   return std::memcmp(expected, calculated, 2) == 0;
}

std::expected<size_t, Writable::Error> ModbusMaster::CRCWritable::writeCRC() {
   uint8_t crc[2];
   digest_.getDigest(crc);
   return sink_.write(crc);
}

std::expected<uint8_t, ResponseStatus> ModbusMaster::parseHeader(uint8_t deviceAddress, Readable &r, uint8_t expectedFunctionCode) {
   r.setTimeout(response_timeout_);

   uint8_t buf[2];
   auto res = r.read(buf);
   if(!res) {
      return std::unexpected(ResponseStatus::ReadTimeout);
   }
   r.setTimeout(packet_timeout_);
   if(buf[0] != deviceAddress) {
      read_state_ = ReadState::WaitForIdle;
      return std::unexpected(ResponseStatus::AddressMismatch);
   }
   auto functionCode = buf[1] & 0x7F;
   if(functionCode != expectedFunctionCode) {
      read_state_ = ReadState::WaitForIdle;
      return std::unexpected(ResponseStatus::FunctionCodeMismatch);
   }
   if(buf[1] & 0x80) {
      // We received an exception
      if(auto exception = r.read()) {
         exception_code_ = exception.value();
      }
      return std::unexpected(ResponseStatus::FunctionCodeException);
   }
   // Determine the size of the response
   switch(functionCode) {
   case FunctionCode::ReadCoils:
   case FunctionCode::ReadDiscreteInputs:
   case FunctionCode::ReadInputRegisters:
   case FunctionCode::ReadHoldingRegisters:
   case FunctionCode::ReadWriteMultipleRegisters: {
      res = r.read();
      if(!res) {
         return std::unexpected(ResponseStatus::ReadTimeout);
      }
   }

   case FunctionCode::WriteSingleCoil:
   case FunctionCode::WriteMultipleCoils:
   case FunctionCode::WriteSingleHoldingRegister:
   case FunctionCode::WriteMultipleHoldingRegisters:
      return 4;

   case FunctionCode::MaskWriteRegister:
      return 6;

   default:
      return 0;
   }
}

std::expected<uint8_t, ResponseStatus>
ModbusMaster::parseResponse(uint8_t deviceAddress, std::span<uint8_t> buffer, uint8_t expectedFunctionCode) {
   CRCReadable crcReadable(readable_);
   // First parse the header
   auto res = parseHeader(deviceAddress, crcReadable, expectedFunctionCode);
   if(!res) {
      // log.warn("Could not parse response header");
      return res;
   }

   // Then read the rest of the response
   const size_t byteCount = res.value();
   if(byteCount > buffer.size()) {
      read_state_ = ReadState::WaitForIdle;
      return std::unexpected(ResponseStatus::BufferOverflow);
   }
   const size_t read = std::min(byteCount, buffer.size());
   for(size_t i = 0; i < read; i++) {
      auto readByte = crcReadable.read();
      if(!readByte) {
         // log.warn("Not receiving expected amount of bytes");
         return std::unexpected(ResponseStatus::ReadTimeout);
      }
      buffer[i] = readByte.value();
   }

   // Check the packet CRC
   if(!crcReadable.readCRCAndValidate()) {
      read_state_ = ReadState::WaitForIdle;
      return std::unexpected(ResponseStatus::CRCMismatch);
   }
   // Everything went Ok
   return read;
}

std::expected<uint8_t, ResponseStatus>
ModbusMaster::readRegisters(uint8_t deviceAddress, FunctionCode code, uint16_t address, std::span<uint8_t> buffer) {
   waitForIdle();
   writable_.flush();
   enableTx();
   CRCWritable crcWritable(writable_);
   uint8_t header[6];
   // Device address
   header[0] = deviceAddress;
   // Function code
   header[1] = code;
   // Register address
   convertEndianToArray<Endian::Big>(address, header, 2);
   // Register count
   convertEndianToArray<Endian::Big, uint16_t>(buffer.size() / 2, header, 4);
   auto res = crcWritable.write(header);
   if(!res) {
      return std::unexpected(ResponseStatus::WriteError);
   }
   res = crcWritable.writeCRC();
   if(!res) {
      return std::unexpected(ResponseStatus::WriteError);
   }

   writable_.flush();
   enableRx();

   return parseResponse(deviceAddress, buffer, code);
}


std::expected<uint8_t, ResponseStatus>
ModbusMaster::writeMultipleRegisters(uint8_t deviceAddress, uint16_t startAddress, std::span<const uint8_t> values) {
   waitForIdle();
   writable_.flush();
   enableTx();
   CRCWritable crcWritable(writable_);
   uint8_t header[7];
   // Device address
   header[0] = deviceAddress;
   // Function code
   header[1] = FunctionCode::WriteMultipleHoldingRegisters;
   // Register address
   convertEndianToArray<Endian::Big, uint16_t>(startAddress, header, 2);
   // Number of registers
   convertEndianToArray<Endian::Big, uint16_t>(values.size() / 2, header, 4);
   // Number of bytes remaining in message
   header[6] = static_cast<uint8_t>(values.size());
   // Write header
   auto res = crcWritable.write(header);
   if(!res) {
      return std::unexpected(ResponseStatus::WriteError);
   }
   res = crcWritable.write(values);
   if(!res) {
      return std::unexpected(ResponseStatus::WriteError);
   }
   res = crcWritable.writeCRC();
   if(!res) {
      return std::unexpected(ResponseStatus::WriteError);
   }
   enableRx();
   uint8_t response[4];
   // don't expect a response on a broadcast
   if(deviceAddress == 0) {
      return 0;
   }
   return parseResponse(deviceAddress, response, FunctionCode::WriteMultipleHoldingRegisters);
}

std::expected<uint8_t, ResponseStatus>
ModbusMaster::writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress, uint16_t value) {
   waitForIdle();
   writable_.flush();
   enableTx();
   uint8_t header[4];
   // Device address
   header[0] = deviceAddress;
   // Function code
   header[1] = FunctionCode::WriteSingleHoldingRegister;
   // Register address
   convertEndianToArray<Endian::Big>(registerAddress, header, 2);
   CRCWritable crcWritable(writable_);
   auto res = crcWritable.write(header);
   if(!res) {
      return std::unexpected(ResponseStatus::WriteError);
   }
   res = crcWritable.writeNumber<uint16_t, Endian::Big>(value);
   if(!res) {
      return std::unexpected(ResponseStatus::WriteError);
   }
   res = crcWritable.writeCRC();
   if(!res) {
      return std::unexpected(ResponseStatus::WriteError);
   }

   enableRx();
   uint8_t response[4];
   // don't expect a response on a broadcast
   if(deviceAddress == 0) {
      return 0;
   }
   return parseResponse(deviceAddress, response, FunctionCode::WriteSingleHoldingRegister);
}

void ModbusMaster::waitForIdle() {
   utils::Timeout packet_timeout(packet_timeout_);
   while(read_state_ == ReadState::WaitForIdle && !packet_timeout.hasExpired()) {
      // Wait
   }
   if(buffered_readable_ != nullptr) {
      buffered_readable_->clear();
   }
   read_state_ = ReadState::Listening;
}


} // namespace modbus
