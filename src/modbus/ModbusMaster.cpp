#include "ModbusMaster.h"

#include <streams/ArrayReadable.h>
#include <utils/endian.h>

namespace modbus {


ModbusMaster::ModbusMaster(Readable &readable, Writable &writable)
   : readable_(readable)
   , writable_(writable) {
   enableRX();
}

bool ModbusMaster::CRCReadable::checkCRC() {
   uint16_t expected;
   digest_.getDigest(std::span(reinterpret_cast<uint8_t *>(&expected), 2));
   uint16_t crc;
   if(auto res = read(std::span(reinterpret_cast<uint8_t *>(&crc), 2)); !res) {
      return false;
   }
   return crc == expected;
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
      case FunctionCode::WriteSingleRegister:
      case FunctionCode::WriteMultipleRegisters:
         return std::expected<uint8_t, ResponseStatus>(4);

      case FunctionCode::MaskWriteRegister:
         return std::expected<uint8_t, ResponseStatus>(6);

      default:
         return std::expected<uint8_t, ResponseStatus>(0);
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
      if(readByte) {
         // log.warn("Not receiving expected amount of bytes");
         return std::unexpected(ResponseStatus::ReadTimeout);
      }
      buffer[i] = readByte.value();
   }

   // Check the packet CRC
   if(!crcReadable.checkCRC()) {
      return std::unexpected(ResponseStatus::CRCMismatch);
   }
   // Everything went Ok
   return read;
}


void ModbusMaster::enableTX() {
   // if(_rxe != nullptr) {
   //    _rxe->deactivate();
   // }
   // _txe->activate();
   // // We need to wait after enabling tx, for the lines to settle. Otherwise, the slave may interpret the line change
   // // as a bit, and the first message we send will not be interpreted properly
   // delaymS(5);
}

void ModbusMaster::enableRX() {
   // _txe->deactivate();
   // if(_rxe != nullptr) {
   //    _rxe->activate();
   // }
}

std::expected<uint8_t, ResponseStatus> ModbusMaster::readRegisters(uint8_t type, uint16_t startAddress, std::span<uint8_t> buffer) {
   enableTX();
   writable_.flush();
   {
      CRCWritable crcWritable(writable_);
      auto result = crcWritable.write(_address)
                    .and_then([&](auto) { return crcWritable.write(type); })
                    .and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(startAddress); })
                    .and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(buffer.size() / 2); })
                    .and_then([&](auto) { return crcWritable.writeCRC(); });

      if(!result) {
         return std::unexpected(ResponseStatus::WriteError);
      }
      writable_.flush();
   }
   enableRX();

   return parseResponse(buffer, type);
}


std::expected<uint8_t, ResponseStatus> ModbusMaster::readRegisters(uint16_t startAddress, std::span<uint8_t> buffer) {
   enableTX();
   writable_.flush();
   CRCWritable crcWritable(writable_);
   auto result = crcWritable.write(_address)
                 .and_then([&](auto) { return crcWritable.write(FunctionCode::ReadWriteMultipleRegisters); })
                 .and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(startAddress); })
                 .and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(buffer.size() / 2); })
                 .and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(startAddress); })
                 .and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(0); })
                 .and_then([&](auto) { return crcWritable.write(0); })
                 .and_then([&](auto) { return crcWritable.writeCRC(); });

   if(!result) {
      return std::unexpected(ResponseStatus::WriteError);
      writable_.flush();
   }
   // Switch to reading again

   enableRX();

   return parseResponse(buffer, FunctionCode::ReadWriteMultipleRegisters);
}

std::expected<uint8_t, ResponseStatus> ModbusMaster::writeMultipleRegisters(uint16_t address, std::span<const uint8_t> values) {
   enableTX();
   writable_.flush();

   CRCWritable crcWritable(writable_);
   auto result = crcWritable.write(_address)
                 .and_then([&](auto) { return crcWritable.write(FunctionCode::WriteMultipleRegisters); })
                 .and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(address); })
                 .and_then([&](auto) { return crcWritable.writeNumber<uint16_t, Endian::Big>(values.size() / 2); })
                 .and_then([&](auto) { return crcWritable.write(values.size()); })
                 .and_then([&](auto) {
                    for(auto value : values) {
                       if(!crcWritable.write(value)) {
                          return crcWritable.write(value);
                       }
                    }
                    return crcWritable.writeCRC();
                 });

   if(!result) {
      writable_.flush();
      return std::unexpected(ResponseStatus::WriteError);
   }
   enableRX();
   uint8_t response[4];
   // don't expect a response on a broadcast
   if(_address == 0) {
      return 0;
   }
   return parseResponse(response, FunctionCode::WriteMultipleRegisters);
}

std::expected<uint8_t, ResponseStatus> ModbusMaster::writeSingleRegister(uint16_t address, uint16_t value) {
   return writeMultipleRegisters(address, { reinterpret_cast<uint8_t *>(&value), 2 });
}

} // namespace modbus
