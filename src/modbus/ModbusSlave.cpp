#include "ModbusSlave.h"

#include <array>

namespace modbus {

bool ModbusSlave::CRCReadable::readCRCAndValidate() {
   uint16_t expected;
   digest_.getDigest(std::span(reinterpret_cast<uint8_t *>(&expected), 2));
   uint16_t crc;
   // Read CRC from source (low byte first as produced by IBMCRC16Digest)
   auto lo = Readable::read();
   if(!lo)
      return false;
   auto hi = Readable::read();
   if(!hi)
      return false;
   crc = static_cast<uint16_t>(static_cast<uint8_t>(lo.value())) | (static_cast<uint16_t>(static_cast<uint8_t>(hi.value())) << 8);
   return crc == expected;
}

std::expected<size_t, Writable::Error> ModbusSlave::CRCWritable::writeCRC() {
   uint8_t crc[2];
   digest_.getDigest(crc);
   return sink_.write(crc);
}


std::expected<void, Writable::Error> ModbusSlave::writeException(uint8_t function, uint8_t exceptionCode) {
   CRCWritable w(writable_);
   auto res = w.write(address_)
              .and_then([&](auto) { return w.write(static_cast<uint8_t>(function | 0x80)); })
              .and_then([&](auto) { return w.write(exceptionCode); })
              .and_then([&](auto) { return w.writeCRC(); })
              .transform([](auto) { return; });
   writable_.flush();
   return res;
}

std::expected<void, Writable::Error> ModbusSlave::writeReadResponse(uint8_t function, std::span<const uint16_t> regs) {
   CRCWritable w(writable_);
   auto res = w.write(address_)
              .and_then([&](auto) { return w.write(function); })
              .and_then([&](auto) { return w.write(static_cast<uint8_t>(regs.size() * 2)); })
              .and_then([&](auto) {
                 for(uint16_t v : regs) {
                    auto r = w.writeNumber<uint16_t, Endian::Big>(v);
                    if(!r)
                       return r; // propagate error
                 }
                 return std::expected<size_t, Writable::Error>(0);
              })
              .and_then([&](auto) { return w.writeCRC(); })
              .transform([](auto) { return; });
   writable_.flush();
   return res;
}

std::expected<void, Writable::Error> ModbusSlave::writeWriteMultiResponse(uint16_t start, uint16_t count) {
   CRCWritable w(writable_);
   auto res = w.write(address_)
              .and_then([&](auto) { return w.write(FunctionCode::WriteMultipleHoldingRegisters); })
              .and_then([&](auto) { return w.writeNumber<uint16_t, Endian::Big>(start); })
              .and_then([&](auto) { return w.writeNumber<uint16_t, Endian::Big>(count); })
              .and_then([&](auto) { return w.writeCRC(); })
              .transform([](auto) { return; });
   writable_.flush();
   return res;
}

bool ModbusSlave::readInputRegisters(ModbusSlave::CRCReadable r, const uint8_t function, const bool isBroadcast) {
   auto startRes = r.readNumber<uint16_t, Endian::Big>();
   if(!startRes)
      return false;
   auto countRes = r.readNumber<uint16_t, Endian::Big>();
   if(!countRes)
      return false;
   const uint16_t start = startRes.value();
   const uint16_t count = countRes.value();
   if(count == 0 || count > 125) {
      // invalid quantity of registers
      (void)r.readCRCAndValidate();
      if(!isBroadcast)
         (void)writeException(function, ResultCode::ILLEGAL_VALUE);
      return true;
   }
   if(!r.readCRCAndValidate()) {
      return true; // bad CRC, drop silently
   }
   auto &cb = (function == FunctionCode::ReadHoldingRegisters) ? readHolding_ : readInput_;
   if(!cb) {
      if(!isBroadcast)
         (void)writeException(function, ResultCode::ILLEGAL_FUNCTION);
      return true;
   }
   std::array<uint16_t, 125> temp{};
   std::span<uint16_t> out(temp.data(), count);
   if(!cb(start, count, out)) {
      if(!isBroadcast)
         (void)writeException(function, ResultCode::ILLEGAL_ADDRESS);
      return true;
   }
   if(!isBroadcast) {
      (void)writeReadResponse(function, std::span<const uint16_t>(out.data(), count));
   }
   return true;
}
bool ModbusSlave::writeMultipleRegisters(ModbusSlave::CRCReadable r, const uint8_t function, const bool isBroadcast) {
   auto startRes = r.readNumber<uint16_t, Endian::Big>();
   if(!startRes)
      return false;
   auto countRes = r.readNumber<uint16_t, Endian::Big>();
   if(!countRes)
      return false;
   const uint16_t start = startRes.value();
   const uint16_t count = countRes.value();
   auto byteCount = r.Readable::read();
   if(!byteCount)
      return false;
   const uint8_t bc = static_cast<uint8_t>(byteCount.value());
   if(count == 0 || count > 123 || bc != count * 2) {
      (void)r.readCRCAndValidate();
      if(!isBroadcast)
         (void)writeException(function, ResultCode::ILLEGAL_VALUE);
      return true;
   }
   std::array<uint16_t, 123> values{};
   for(uint16_t i = 0; i < count; ++i) {
      auto reg = r.readNumber<uint16_t, Endian::Big>();
      if(!reg)
         return false;
      values[i] = reg.value();
   }
   if(!r.readCRCAndValidate()) {
      return true;
   }
   if(!writeHolding_) {
      if(!isBroadcast)
         (void)writeException(function, ResultCode::ILLEGAL_FUNCTION);
      return true;
   }
   const bool ok = writeHolding_(start, std::span<const uint16_t>(values.data(), count));
   if(!ok) {
      if(!isBroadcast)
         (void)writeException(function, ResultCode::ILLEGAL_ADDRESS);
      return true;
   }
   if(!isBroadcast) {
      (void)writeWriteMultiResponse(start, count);
   }
   return true;
}

bool ModbusSlave::poll() {
   CRCReadable r(readable_);
   // Wait for first byte of a possible frame
   r.setTimeout(requestTimeout_);
   auto a = r.read();
   if(!a)
      return false; // no data / timeout
   const auto rxAddress = static_cast<uint8_t>(a.value());

   // Read function code
   r.setTimeout(interCharTimeout_);
   auto f = r.read();
   if(!f)
      return false; // framing error
   const uint8_t function = static_cast<uint8_t>(f.value());

   // Determine if this is for us or broadcast
   const bool isBroadcast = (rxAddress == 0);
   if(rxAddress != address_ && !isBroadcast) {
      // Still consume frame payload+CRC to keep stream in sync, but ignore it
      // For simplicity, we will drop it by not reading further (next poll will resync on address).
      return false;
   }

   // Parse and act
   switch(function) {
   case FunctionCode::ReadHoldingRegisters:
   case FunctionCode::ReadInputRegisters: {
      return readInputRegisters(r, function, isBroadcast);
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
