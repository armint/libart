#include "ModbusSlave.h"
#include "ModbusCodes.h"

#include <streams/ArrayReadable.h>

#include <array>

namespace modbus {

ModbusSlave::ModbusSlave(Readable &readable, Writable &writable, std::span<uint8_t> buffer, enabler_t rxEnabler, enabler_t txEnabler)
   : readable_(readable)
   , writable_(writable)
   , buffer_(buffer)
   , packet_timeout_(300)
   , txEnabler_(txEnabler)
   , rxEnabler_(rxEnabler) {
   readable_.setTimeout(20);
}

ModbusSlave::ModbusSlave(BufferedReadable &readable, Writable &writable, std::span<uint8_t> buffer, enabler_t rxEnabler, enabler_t txEnabler)
   : readable_(readable)
   , buffered_readable_(&readable)
   , writable_(writable)
   , buffer_(buffer)
   , packet_timeout_(300)
   , txEnabler_(txEnabler)
   , rxEnabler_(rxEnabler) {
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
   uint8_t header[3] = { address_, static_cast<uint8_t>(function | 0x80), exceptionCode };
   auto res = w.write(header);
   if(!res) {
      return res;
   }
   auto count = res.value();
   res = w.writeCRC();
   if(!res) {
      return res;
   }
   writable_.flush();
   return count + 2;
}

bool ModbusSlave::handleReadRegisters(ModbusSlave::CRCReadable &r, const uint8_t function, const bool isBroadcast) {
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
   auto cb_arg = (function == FunctionCode::ReadHoldingRegisters) ? read_holding_arg_ : read_input_arg_;
   if(!cb) {
      if(!isBroadcast) {
         (void)writeException(function, ResultCode::ILLEGAL_FUNCTION);
      }
      return true;
   }
   if(!isBroadcast) {
      CRCWritable w(writable_);
      uint8_t header[3] = { address_, function, static_cast<uint8_t>(count * 2) };
      auto res = w.write(header);
      if(!res) {
         // TODO logging!
         return true;
      }
      if(cb(cb_arg, start, count, w) == Result::AddressOutOfRange) {
         writeException(function, ResultCode::ILLEGAL_ADDRESS);
         return true;
      }
      w.writeCRC();
      writable_.flush();

      if(!res) {
         // TODO logging!
      }
   }
   return true;
}
bool ModbusSlave::handleWriteMultipleRegisters(ModbusSlave::CRCReadable &r, const uint8_t function, const bool isBroadcast) {
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
   if(count == 0 || byteCount > buffer_.size() || byteCount != count * 2) {
      if(!isBroadcast) {
         r.readCRCAndValidate();
         (void)writeException(function, ResultCode::ILLEGAL_VALUE);
      }
      return true;
   }
   // Read the data into the buffer
   auto readResult = r.read(buffer_.subspan(0, byteCount));
   if(!readResult) {
      return false;
   }
   // Check the CRC
   if(!r.readCRCAndValidate()) {
      return true;
   }

   if(!writeHolding_) {
      if(!isBroadcast)
         writeException(function, ResultCode::ILLEGAL_FUNCTION);
      return true;
   }
   // Send the data to the user application
   ArrayReadable readable(buffer_.subspan(0, byteCount));
   const Result ok = writeHolding_(write_holding_arg_, start, readable);
   if(ok != Result::Ok) {
      if(!isBroadcast) {
         ResultCode code;
         switch(ok) {
         case Result::AddressOutOfRange:
            code = ILLEGAL_ADDRESS;
            break;
         case Result::IllegalValue:
            code = ILLEGAL_VALUE;
            break;
         case Result::CouldNotRead:
         default:
            code = FAILURE;
         }
         writeException(function, code);
      }
      return true;
   }
   // Send a response
   if(!isBroadcast) {
      CRCWritable w(writable_);
      uint8_t response[6];
      response[0] = address_;
      response[1] = FunctionCode::WriteMultipleHoldingRegisters;
      convertEndianToArray<Endian::Big, uint16_t>(start, response, 2);
      convertEndianToArray<Endian::Big, uint16_t>(count, response, 4);
      auto res = w.write(response);
      if(!res) {
         // TODO LOGGING!
      } else {
         res = w.writeCRC();
      }
      if(!res) {
         // TODO LOGGING!
      }
      writable_.flush();
   }
   return true;
}

bool ModbusSlave::isWaitingForIdle() {
   if(read_state_ == ReadState::WaitForIdle && !packet_timeout_.hasExpired()) {
      return true;
   }
   if(read_state_ == ReadState::WaitForIdle) {
      if(buffered_readable_ != nullptr) {
         buffered_readable_->clear();
      }
      read_state_ = ReadState::Listening;
   }
   return false;
}

bool ModbusSlave::poll() {
   enableRx(true);
   if(isWaitingForIdle()) {
      return false;
   }
   if(!readable_.available()) {
      return false;
   }
   CRCReadable r(readable_);
   auto address = r.read();
   if(!address) {
      return false; // no data / timeout
   }
   const auto rxAddress = static_cast<uint8_t>(address.value());
   // Read function code
   auto f = r.read();
   if(!f) {
      // framing error
      read_state_ = ReadState::WaitForIdle;
      return false;
   }
   const uint8_t function = static_cast<uint8_t>(f.value());

   // Determine if this is for us or broadcast
   const bool isBroadcast = (rxAddress == 0);
   if(rxAddress != address_ && !isBroadcast) {
      // Still consume frame payload+CRC to keep stream in sync, but ignore it
      read_state_ = ReadState::WaitForIdle;
      if(rxEnabler_) {
         enableRx(false);
      }
      return false;
   }

   // Parse and act
   switch(function) {
   case FunctionCode::ReadHoldingRegisters:
   case FunctionCode::ReadInputRegisters: {
      return handleReadRegisters(r, function, isBroadcast);
   }
   case FunctionCode::WriteMultipleHoldingRegisters: {
      return handleWriteMultipleRegisters(r, function, isBroadcast);
   }
   default:
      // Unsupported function
      read_state_ = ReadState::WaitForIdle;

      (void)r.readCRCAndValidate();
      if(!isBroadcast) {
         writeException(function, ResultCode::ILLEGAL_FUNCTION);
      }
      return true;
   }
}

} // namespace modbus
