#include "ModbusSlave.h"

#include <array>

namespace modbus {

ModbusSlave::ModbusSlave(Readable &readable, Writable &writable, std::span<uint16_t> buffer, enabler_t rxEnabler, enabler_t txEnabler)
   : readable_(readable)
   , writable_(writable)
   , buffer_(buffer)
   , rxEnabler_(std::move(rxEnabler))
   , txEnabler_(std::move(txEnabler)) {
   readable_.setTimeout(20);
}

bool ModbusSlave::CRCReadable::readCRCAndValidate() {
   uint8_t expected[2];
   digest_.getDigest(std::span(expected, 2));
   uint8_t calculated[2];
   if(auto res = read(calculated); !res) {
      return false;
   }
   return std::memcmp(expected, calculated, 2) == 0;
}

std::expected<size_t, Writable::Error> ModbusSlave::CRCWritable::writeCRC() {
   uint8_t crc[2];
   digest_.getDigest(crc);
   return sink_.write(crc);
}


std::expected<size_t, Writable::Error> ModbusSlave::writeException(uint8_t function, uint8_t exceptionCode) {
   CRCWritable w(writable_);
   auto res = w.write(address_)
              .and_then([&](auto) { return w.write(static_cast<uint8_t>(function | 0x80)); })
              .and_then([&](auto) { return w.write(exceptionCode); })
              .and_then([&](auto) { return w.writeCRC(); })
              .transform([](auto) { return 4; });
   writable_.flush();
   return res;
}

bool ModbusSlave::readRegisters(ModbusSlave::CRCReadable& r, const uint8_t function, const bool isBroadcast) {
   auto startRes = r.readNumber<uint16_t, Endian::Big>();
   if(!startRes) {
      return false;
   }
   auto countRes = r.readNumber<uint16_t, Endian::Big>();
   if(!countRes) {
      return false;
   }
   const uint16_t start = startRes.value();
   const uint16_t count = countRes.value();
   if(!r.readCRCAndValidate()) {
      // Bad CRC, drop request
      return true;
   }
   if(count == 0 || count > 125) {
      // invalid quantity of registers
      if(!isBroadcast) {
         (void)writeException(function, ResultCode::ILLEGAL_VALUE);
      }
      return true;
   }

   auto &cb = (function == FunctionCode::ReadHoldingRegisters) ? readHolding_ : readInput_;
   if(!cb) {
      if(!isBroadcast) {
         (void)writeException(function, ResultCode::ILLEGAL_FUNCTION);
      }
      return true;
   }
   if(!isBroadcast) {
      CRCWritable w(writable_);
      auto res = w.write(address_)
                 .and_then([&](auto) { return w.write(function); })
                 .and_then([&](auto) { return w.write(static_cast<uint8_t>(count * 2)); })
                 .and_then([&](auto) {
                    if(!cb(start, count, w)) {
                       return writeException(function, ResultCode::ILLEGAL_ADDRESS);
                    }
                    return std::expected<size_t, Writable::Error>(0);
                 })
                 .and_then([&](auto) { return w.writeCRC(); })
                 .transform([](auto) { return; });
      writable_.flush();

      if(!res) {
         // TODO logging!
      }
   }
   return true;
}
bool ModbusSlave::writeMultipleRegisters(ModbusSlave::CRCReadable& r, const uint8_t function, const bool isBroadcast) {
   auto startRes = r.readNumber<uint16_t, Endian::Big>();
   if(!startRes) {
      return false;
   }
   auto countRes = r.readNumber<uint16_t, Endian::Big>();
   if(!countRes) {
      return false;
   }
   const uint16_t start = startRes.value();
   const size_t count = countRes.value();
   auto byteCountRes = r.read();
   if(!byteCountRes) {
      return false;
   }
   const uint8_t byteCount = static_cast<uint8_t>(byteCountRes.value());
   // We cannot accept more registers than we can buffer
   if(count == 0 || count > std::size(buffer_) || byteCount != count * 2) {
      if(!isBroadcast) {
         r.readCRCAndValidate();
         (void)writeException(function, ResultCode::ILLEGAL_VALUE);
      }
      return true;
   }
   // Read the data into the buffer
   for(size_t i = 0; i < count; ++i) {
      auto reg = r.readNumber<uint16_t, Endian::Big>();
      if(!reg) {
         return false;
      }
      buffer_[i] = reg.value();
   }
   // Check the CRC
   if(!r.readCRCAndValidate()) {
      return true;
   }

   if(!writeHolding_) {
      if(!isBroadcast)
         (void)writeException(function, ResultCode::ILLEGAL_FUNCTION);
      return true;
   }
   // Send the data to the user application
   const bool ok = writeHolding_(start, buffer_.subspan(0, count));
   if(!ok) {
      if(!isBroadcast) {
         (void)writeException(function, ResultCode::ILLEGAL_ADDRESS);
      }
      return true;
   }
   // Send a response
   if(!isBroadcast) {
      CRCWritable w(writable_);
      auto res = w.write(address_)
                 .and_then([&](auto) { return w.write(FunctionCode::WriteMultipleHoldingRegisters); })
                 .and_then([&](auto) { return w.writeNumber<uint16_t, Endian::Big>(start); })
                 .and_then([&](auto) { return w.writeNumber<uint16_t, Endian::Big>(count); })
                 .and_then([&](auto) { return w.writeCRC(); })
                 .transform([](auto) { return; });
      writable_.flush();
      if(!res) {
         // TODO LOGGING!
      }
   }
   return true;
}

bool ModbusSlave::poll() {
   CRCReadable r(readable_);
   if(!r.available()) {
      return false;
   }
   auto a = r.read();
   if(!a) {
      return false; // no data / timeout
   }
   const auto rxAddress = static_cast<uint8_t>(a.value());
   // Read function code
   auto f = r.read();
   if(!f) {
      return false; // framing error
   }
   const uint8_t function = static_cast<uint8_t>(f.value());

   // Determine if this is for us or broadcast
   const bool isBroadcast = (rxAddress == 0);
   if(rxAddress != address_ && !isBroadcast) {
      // Still consume frame payload+CRC to keep stream in sync, but ignore it
      // For simplicity, we will drop it by not reading further (next poll will resync on address).
      if(rxEnabler_) {
         enableRx(false);
         return false;
      }
   }

   // Parse and act
   switch(function) {
   case FunctionCode::ReadHoldingRegisters:
   case FunctionCode::ReadInputRegisters: {
      return readRegisters(r, function, isBroadcast);
   }
   case FunctionCode::WriteMultipleHoldingRegisters: {
      return writeMultipleRegisters(r, function, isBroadcast);
   }
   default:
      // Unsupported function
      (void)r.readCRCAndValidate();
      if(!isBroadcast) {
         (void)writeException(function, ResultCode::ILLEGAL_FUNCTION);
      }
      return true;
   }
}

} // namespace modbus
