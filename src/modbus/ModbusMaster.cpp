#include "ModbusMaster.h"

#include <streams/ArrayReadable.h>
#include <utils/endian.h>

namespace modbus {


ModbusMaster::ModbusMaster(Readable &readable, Writable &writable)
   : readable_(readable)
   , writable_(writable) {
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

std::expected<uint8_t, ResponseStatus> ModbusMaster::parseHeader(Readable &r, uint8_t expectedFunctionCode) {
   r.setTimeout(_responseTimeout);

   return r.read()
   .transform_error([](auto) { return ResponseStatus::ReadTimeout; })
   .and_then([&](uint8_t address) -> std::expected<uint8_t, ResponseStatus> {
      if(address != _address) {
         return std::unexpected(ResponseStatus::AddressMismatch);
      }
      r.setTimeout(symbolTimeout_);
      return r.read().transform_error([](auto) { return ResponseStatus::ReadTimeout; });
   })
   .and_then([&](uint8_t code) -> std::expected<uint8_t, ResponseStatus> {
      auto functionCode = code & 0x7F;
      if(functionCode != expectedFunctionCode) {
         return std::unexpected(ResponseStatus::FunctionCodeMismatch);
      }
      if(code & 0x80) {
         // We received an exception
         if(auto exception = r.read()) {
            _exceptionCode = exception.value();
         }
         return std::unexpected(ResponseStatus::FunctionCodeException);
      }

      // Determine the size of the response
      switch(functionCode) {
      case FunctionCode::ReadCoils:
      case FunctionCode::ReadDiscreteInputs:
      case FunctionCode::ReadInputRegisters:
      case FunctionCode::ReadHoldingRegisters:
      case FunctionCode::ReadWriteMultipleRegisters:
         return r.read().transform_error([](auto) { return ResponseStatus::ReadTimeout; });

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
   });
}

std::expected<uint8_t, ResponseStatus> ModbusMaster::parseResponse(std::span<uint8_t> buffer, uint8_t expectedFunctionCode) {
   CRCReadable crcReadable(readable_);
   // First parse the header
   auto res = parseHeader(crcReadable, expectedFunctionCode);
   if(!res) {
      // log.warn("Could not parse response header");
      return res;
   }

   // Then read the rest of the response
   const size_t byteCount = res.value();
   if(byteCount > buffer.size()) {
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
      return std::unexpected(ResponseStatus::CRCMismatch);
   }
   // Everything went Ok
   return read;
}

std::expected<uint8_t, ResponseStatus> ModbusMaster::readRegisters(FunctionCode code, uint16_t address, std::span<uint8_t> buffer) {
   enableTx();
   writable_.flush();
   CRCWritable crcWritable(writable_);
   uint8_t header[6];
   // Device address
   header[0] = _address;
   // Function code
   header[1] = code;
   // Register address
   convertEndianToArray<Endian::Big>(address, header, 2);
   // Register count
   convertEndianToArray<Endian::Big, uint16_t>(buffer.size() / 2, header, 4);

   auto result = crcWritable.write(header)
                 .and_then([&](auto) { return crcWritable.writeCRC(); });

   if(!result) {
      return std::unexpected(ResponseStatus::WriteError);
   }
   writable_.flush();
   enableRx();

   return parseResponse(buffer, code);
}


std::expected<uint8_t, ResponseStatus> ModbusMaster::writeMultipleRegisters(uint16_t address, std::span<const uint8_t> values) {
   enableTx();
   writable_.flush();
   CRCWritable crcWritable(writable_);
   uint8_t header[7];
   // Device address
   header[0] = _address;
   // Function code
   header[1] = FunctionCode::WriteMultipleHoldingRegisters;
   // Register address
   convertEndianToArray<Endian::Big, uint16_t>(address, header, 2);
   // Number of registers
   convertEndianToArray<Endian::Big, uint16_t>(values.size() / 2, header, 4);
   // Number of bytes remaining in message
   header[6] = static_cast<uint8_t>(values.size());
   // Write header
   auto result = crcWritable.write(header)
                 .and_then([&](auto) {
                    // write data
                    return crcWritable.write(values);
                 })
                 .and_then([&](auto) {
                    // write CRC
                    return crcWritable.writeCRC();
                 });

   if(!result) {
      writable_.flush();
      return std::unexpected(ResponseStatus::WriteError);
   }
   enableRx();
   uint8_t response[4];
   // don't expect a response on a broadcast
   if(_address == 0) {
      return 0;
   }
   return parseResponse(response, FunctionCode::WriteMultipleHoldingRegisters);
}

std::expected<uint8_t, ResponseStatus> ModbusMaster::writeSingleRegister(uint16_t address, uint16_t value) {
   enableTx();
   writable_.flush();
   uint8_t header[4];
   // Device address
   header[0] = _address;
   // Function code
   header[1] = FunctionCode::WriteSingleHoldingRegister;
   // Register address
   convertEndianToArray<Endian::Big>(address, header, 2);
   CRCWritable crcWritable(writable_);
   auto result =
   crcWritable.write(header).and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(value); }).and_then([&](auto) {
      return crcWritable.writeCRC();
   });
   if(!result) {
      writable_.flush();
      return std::unexpected(ResponseStatus::WriteError);
   }
   enableRx();
   uint8_t response[4];
   // don't expect a response on a broadcast
   if(_address == 0) {
      return 0;
   }
   return parseResponse(response, FunctionCode::WriteSingleHoldingRegister);
}

} // namespace modbus
