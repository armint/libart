#include "modbus/ModbusMaster.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include <doctest/doctest.h>

#include <modbus/ModbusCodes.h>
#include <modbus/ModbusSlave.h>
#include <streams/ArrayReadable.h>
#include <streams/ArrayWritable.h>
#include <utils/IBMCRC16Digest.h>

using namespace modbus;

static void compute_crc(art::span<const uint8_t> frame_wo_crc, uint8_t (&crc)[2]) {
   IBMCRC16Digest d;
   d.update(frame_wo_crc);
   (void)d.getDigest(crc);
}

TEST_CASE("[Modbus] Slave replies to Read Holding Registers (2 regs) [Modbus]") {
   // Build a RTU request: addr=0x01, fn=0x03, start=0x0000, qty=0x0002, CRC=0x0B after 0xC4 (lo,hi)
   const uint8_t request[] = { 0x01, FunctionCode::ReadHoldingRegisters, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B };
   ArrayReadable readable(request);

   // Capture the slave response
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   // Modbus slave with small temp buffer (not used for reads)
   uint8_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);

   // Provide a read-holding callback that writes two registers: 0x1122 and 0x3344
   slave.setReadHoldingCallback(
   +[](void *arg, uint16_t address, uint16_t count, Writable &w) -> modbus::ModbusSlave::Result {
      (void)arg;
      REQUIRE(address == 0x0000);
      REQUIRE(count == 2);
      // Two registers, big-endian per register
      (void)w.writeNumber<uint16_t, Endian::Big>(0x1122);
      (void)w.writeNumber<uint16_t, Endian::Big>(0x3344);
      return ModbusSlave::Result::Ok; // address accepted
   },
   nullptr);

   // Act: process the single frame
   const bool processed = slave.poll();
   REQUIRE(processed);

   // Assert: response TLV + CRC
   const size_t written = writable.position();
   REQUIRE(written == 9); // addr(1)+fn(1)+bytecnt(1)+data(4)+crc(2)

   // Verify header
   REQUIRE(txBuffer[0] == 0x01);
   REQUIRE(txBuffer[1] == FunctionCode::ReadHoldingRegisters);
   REQUIRE(txBuffer[2] == 0x04); // 2 regs -> 4 data bytes

   // Verify payload values (big-endian per register)
   const uint8_t expectedPayload[] = { 0x11, 0x22, 0x33, 0x44 };
   REQUIRE(std::equal(std::begin(expectedPayload), std::end(expectedPayload), &txBuffer[3]));

   // Verify CRC independently
   uint8_t crc[2]{};
   compute_crc({ txBuffer, 7 }, crc); // compute over addr..data
   REQUIRE(txBuffer[7] == crc[0]);    // CRC lo
   REQUIRE(txBuffer[8] == crc[1]);    // CRC hi
}

TEST_CASE("[Modbus] Slave replies to Read Holding Registers (float) [Modbus]") {
   // Build a RTU request: addr=0x01, fn=0x03, start=0x0000, qty=0x0002, CRC=0x0B after 0xC4 (lo,hi)
   const uint8_t request[] = { 0x01, FunctionCode::ReadInputRegisters, 0x00, 0x00, 0x00, 0x02, 0x71, 0xCB };
   ArrayReadable readable(request);
   const float number = 230.2f;

   // Capture the slave response
   uint8_t slaveTxBuffer[32]{};
   ArrayWritable writable(slaveTxBuffer);

   // Modbus slave with small temp buffer (not used for reads)
   uint8_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);

   slave.setReadInputCallback(
   +[](void *arg, uint16_t address, uint16_t count, Writable &w) -> modbus::ModbusSlave::Result {
      REQUIRE(address == 0x0000);
      REQUIRE(count == 2);
      // Two registers, big-endian per register
      w.writeNumber<float>(*reinterpret_cast<float *>(arg));
      return ModbusSlave::Result::Ok; // address accepted
   },
   const_cast<float *>(&number));

   // Act: process the single frame
   const bool processed = slave.poll();
   REQUIRE(processed);

   // Assert: response TLV + CRC
   const size_t written = writable.position();
   REQUIRE(written == 9); // addr(1)+fn(1)+bytecnt(1)+data(4)+crc(2)

   // Verify header
   REQUIRE(slaveTxBuffer[0] == 0x01);
   REQUIRE(slaveTxBuffer[1] == FunctionCode::ReadInputRegisters);
   REQUIRE(slaveTxBuffer[2] == 0x04); // 2 regs -> 4 data bytes

   uint8_t masterTxBuffer[20];
   ArrayReadable masterReadable({ art::span<const uint8_t>(slaveTxBuffer, 9) });
   ArrayWritable masterWritable(masterTxBuffer);
   ModbusMaster master(masterReadable, masterWritable);
   uint8_t masterOut[4];
   auto masterResult = master.readInputRegisters(1, 0, masterOut);
   REQUIRE(masterResult);
   REQUIRE(masterResult.value() == 4);
   float floatValue;
   std::memcpy(&floatValue, masterOut, sizeof(float));
   REQUIRE(floatValue == doctest::Approx(230.2f));
}

TEST_CASE("[Modbus] Slave processes Write Multiple Holding Registers and replies [Modbus]") {
   // Build a RTU request for function 0x10:
   // addr=0x01, fn=0x10, start=0x0002, qty=0x0002, bytecnt=0x04, data=0x11 0x22 0x33 0x44
   uint8_t frame[13]{};
   frame[0] = 0x01;
   frame[1] = FunctionCode::WriteMultipleHoldingRegisters;
   frame[2] = 0x00;
   frame[3] = 0x02; // start
   frame[4] = 0x00;
   frame[5] = 0x02; // qty
   frame[6] = 0x04; // byte count
   frame[7] = 0x11;
   frame[8] = 0x22;
   frame[9] = 0x33;
   frame[10] = 0x44; // data
   {
      uint8_t crc[2]{};
      compute_crc({ frame, 11 }, crc); // compute CRC over addr..data
      frame[11] = crc[0];
      frame[12] = crc[1];
   }

   ArrayReadable readable(frame);

   // Capture the slave response
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   // Provide a buffer large enough to hold incoming payload (bytecount=4)
   uint8_t tmpBuf[8]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);

   bool callbackCalled = false;
   slave.setWriteHoldingCallback(
   +[](void *arg, uint16_t address, BoundedReadable &r) -> modbus::ModbusSlave::Result {
      *static_cast<bool *>(arg) = true;
      REQUIRE(address == 0x0002);
      // Expect exactly 4 bytes for 2 registers
      uint8_t buf[4]{};
      auto res = r.read(buf);
      REQUIRE(res);
      REQUIRE(res.value() == 4);
      const uint8_t expectedData[] = { 0x11, 0x22, 0x33, 0x44 };
      REQUIRE(std::equal(std::begin(expectedData), std::end(expectedData), buf));
      return ModbusSlave::Result::Ok; // accept write
   },
   &callbackCalled);

   // Act
   const bool processed = slave.poll();
   REQUIRE(processed);
   REQUIRE(callbackCalled);

   // Response must echo start and quantity, with CRC
   const size_t written = writable.position();
   REQUIRE(written == 8); // addr(1)+fn(1)+start(2)+qty(2)+crc(2)

   // Verify header
   REQUIRE(txBuffer[0] == 0x01);
   REQUIRE(txBuffer[1] == FunctionCode::WriteMultipleHoldingRegisters);
   REQUIRE(txBuffer[2] == 0x00);
   REQUIRE(txBuffer[3] == 0x02);
   REQUIRE(txBuffer[4] == 0x00);
   REQUIRE(txBuffer[5] == 0x02);

   // Verify CRC
   uint8_t crc[2]{};
   compute_crc({ txBuffer, 6 }, crc);
   REQUIRE(txBuffer[6] == crc[0]);
   REQUIRE(txBuffer[7] == crc[1]);
}

TEST_CASE("[Modbus] Slave processes broadcast Write Multiple without replying [Modbus]") {
   // Broadcast address 0x00, same payload as previous test
   uint8_t frame[13]{};
   frame[0] = 0x00; // broadcast
   frame[1] = FunctionCode::WriteMultipleHoldingRegisters;
   frame[2] = 0x00;
   frame[3] = 0x02;
   frame[4] = 0x00;
   frame[5] = 0x02;
   frame[6] = 0x04;
   frame[7] = 0xAA;
   frame[8] = 0xBB;
   frame[9] = 0xCC;
   frame[10] = 0xDD;
   {
      uint8_t crc[2]{};
      compute_crc({ frame, 11 }, crc);
      frame[11] = crc[0];
      frame[12] = crc[1];
   }

   ArrayReadable readable(frame);
   uint8_t txBuffer[16]{};
   ArrayWritable writable(txBuffer);

   uint8_t tmpBuf[8]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01); // our slave address (ignored due to broadcast)

   bool callbackCalled = false;
   slave.setWriteHoldingCallback(
   +[](void *arg, uint16_t address, BoundedReadable &r) -> modbus::ModbusSlave::Result {
      *static_cast<bool *>(arg) = true;
      REQUIRE(address == 0x0002);
      uint8_t data[4]{};
      auto res = r.read(data);
      REQUIRE(res);
      REQUIRE(res.value() == 4);
      const uint8_t expected[] = { 0xAA, 0xBB, 0xCC, 0xDD };
      REQUIRE(std::equal(std::begin(expected), std::end(expected), data));
      return ModbusSlave::Result::Ok;
   },
   &callbackCalled);

   const bool processed = slave.poll();
   REQUIRE(processed);
   REQUIRE(callbackCalled);
   // Since broadcast, there must be no response
   REQUIRE(writable.position() == 0);
}

TEST_CASE("[Modbus] Slave returns exception on write callback rejection [Modbus]") {
   // Valid request, but application refuses (returns false) -> expect ILLEGAL_ADDRESS exception
   uint8_t frame[13]{};
   frame[0] = 0x01;
   frame[1] = FunctionCode::WriteMultipleHoldingRegisters;
   frame[2] = 0x00;
   frame[3] = 0x10; // start
   frame[4] = 0x00;
   frame[5] = 0x01; // qty=1
   frame[6] = 0x02; // byte count
   frame[7] = 0x12;
   frame[8] = 0x34; // data
   // pad remaining byte to keep array size, though not used
   {
      uint8_t crc[2]{};
      compute_crc({ frame, 9 }, crc);
      frame[9] = crc[0];
      frame[10] = crc[1];
   }

   ArrayReadable readable({ frame, 11 });
   uint8_t txBuffer[16]{};
   ArrayWritable writable(txBuffer);

   uint8_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);

   slave.setWriteHoldingCallback(
   +[](void *arg, uint16_t, BoundedReadable &) -> modbus::ModbusSlave::Result {
      return ModbusSlave::Result::IllegalValue;
   },
   nullptr);

   REQUIRE(slave.poll());

   // Expect exception frame: addr, fn|0x80, code, CRC (total 5 bytes)
   REQUIRE(writable.position() == 5);
   REQUIRE(txBuffer[0] == 0x01);
   REQUIRE(txBuffer[1] == (FunctionCode::WriteMultipleHoldingRegisters | 0x80));
   REQUIRE(txBuffer[2] == ResultCode::ILLEGAL_VALUE);
   uint8_t crc[2]{};
   compute_crc({ txBuffer, 3 }, crc);
   REQUIRE(txBuffer[3] == crc[0]);
   REQUIRE(txBuffer[4] == crc[1]);
}

TEST_CASE("[Modbus] Slave returns exception on invalid byte count [Modbus]") {
   // Malformed: qty=2 but bytecount=3 (should be 4) -> expect ILLEGAL_VALUE
   uint8_t frame[12]{};
   frame[0] = 0x01;
   frame[1] = FunctionCode::WriteMultipleHoldingRegisters;
   frame[2] = 0x00;
   frame[3] = 0x02; // start
   frame[4] = 0x00;
   frame[5] = 0x02; // qty=2
   frame[6] = 0x03; // wrong byte count
   frame[7] = 0xBE;
   frame[8] = 0xEF;
   frame[9] = 0x01; // only 3 data bytes
   {
      uint8_t crc[2]{};
      compute_crc({ frame, 10 }, crc);
      frame[10] = crc[0];
      frame[11] = crc[1];
   }

   ArrayReadable readable(frame);
   uint8_t txBuffer[16]{};
   ArrayWritable writable(txBuffer);

   // Provide large enough buffer so failure is due to mismatch, not size
   uint8_t tmpBuf[10]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);

   // No callback needed; should fail before callback is checked
   REQUIRE(slave.poll());

   // Expect exception frame for ILLEGAL_VALUE
   REQUIRE(writable.position() == 5);
   REQUIRE(txBuffer[0] == 0x01);
   REQUIRE(txBuffer[1] == (FunctionCode::WriteMultipleHoldingRegisters | 0x80));
   REQUIRE(txBuffer[2] == ResultCode::ILLEGAL_VALUE);
   uint8_t crc[2]{};
   compute_crc({ txBuffer, 3 }, crc);
   REQUIRE(txBuffer[3] == crc[0]);
   REQUIRE(txBuffer[4] == crc[1]);
}

TEST_CASE("[Modbus] Slave calls enablers correctly for successful request") {
   struct EnablerLog {
      std::vector<std::pair<const char *, bool>> calls;
   };

   EnablerLog log;

   auto rxEnabler = [](void *arg, bool enable) {
      auto *log = static_cast<EnablerLog *>(arg);
      log->calls.emplace_back("rx", enable);
   };

   auto txEnabler = [](void *arg, bool enable) {
      auto *log = static_cast<EnablerLog *>(arg);
      log->calls.emplace_back("tx", enable);
   };

   const uint8_t request[] = { 0x01, FunctionCode::ReadHoldingRegisters, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A };
   ArrayReadable readable(request);

   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   uint8_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);
   slave.setRxEnabler(rxEnabler, &log);
   slave.setTxEnabler(txEnabler, &log);

   slave.setReadHoldingCallback(
   +[](void *arg, uint16_t address, uint16_t count, Writable &w) -> modbus::ModbusSlave::Result {
      (void)w.writeNumber<uint16_t, Endian::Big>(0xABCD);
      return ModbusSlave::Result::Ok;
   },
   nullptr);

   const bool processed = slave.poll();
   REQUIRE(processed);

   // Expected sequence:
   // 1. poll() -> enableRx(true)
   // 2. handleReadRegisters() -> enableRx(false)
   // 3. handleReadRegisters() -> enableTx(true)
   // 4. flush()
   // 5. enableTx(false)
   // 6. enableRx(true)
   REQUIRE(log.calls.size() == 5);
   CHECK(log.calls[0] == std::make_pair("rx", true));
   CHECK(log.calls[1] == std::make_pair("rx", false));
   CHECK(log.calls[2] == std::make_pair("tx", true));
   CHECK(log.calls[3] == std::make_pair("tx", false));
   CHECK(log.calls[4] == std::make_pair("rx", true));
}

TEST_CASE("[Modbus] Slave does not disable RX on address mismatch") {
   struct EnablerLog {
      std::vector<std::pair<const char *, bool>> calls;
   };

   EnablerLog log;

   auto rxEnabler = [](void *arg, bool enable) {
      auto *log = static_cast<EnablerLog *>(arg);
      log->calls.emplace_back("rx", enable);
   };

   const uint8_t request[] = { 0x02, FunctionCode::ReadHoldingRegisters, 0x00, 0x00, 0x00, 0x01, 0x84, 0x39 };
   ArrayReadable readable(request);

   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   uint8_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);
   slave.setRxEnabler(rxEnabler, &log);

   const bool processed = slave.poll();
   REQUIRE_FALSE(processed);

   // After the fix in poll(), only the initial enableRx(true) should be called.
   REQUIRE(log.calls.size() == 1);
   CHECK(log.calls[0] == std::make_pair("rx", true));
}

namespace {
std::vector<uint8_t> read_request(uint8_t function, uint16_t start, uint16_t count) {
   std::vector<uint8_t> frame = { 0x01,
                                  function,
                                  static_cast<uint8_t>(start >> 8),
                                  static_cast<uint8_t>(start),
                                  static_cast<uint8_t>(count >> 8),
                                  static_cast<uint8_t>(count) };
   uint8_t crc[2];
   compute_crc(frame, crc);
   frame.push_back(crc[0]);
   frame.push_back(crc[1]);
   return frame;
}

std::vector<uint8_t> exception_frame(uint8_t function, uint8_t code) {
   std::vector<uint8_t> frame = { 0x01, static_cast<uint8_t>(function | 0x80), code };
   uint8_t crc[2];
   compute_crc(frame, crc);
   frame.push_back(crc[0]);
   frame.push_back(crc[1]);
   return frame;
}

struct ReadReplyFixture {
   uint8_t txBuffer[64]{};
   ArrayWritable writable{ txBuffer };
   uint8_t tmpBuf[8]{};
   ModbusSlave::Result result = ModbusSlave::Result::Ok;
   uint16_t registers_written = 0;
   int calls = 0;

   std::vector<uint8_t> poll(const std::vector<uint8_t> &request) {
      ArrayReadable readable(request);
      ModbusSlave slave(readable, writable, tmpBuf);
      slave.setReadInputCallback(
      +[](void *arg, uint16_t, uint16_t, Writable &w) -> ModbusSlave::Result {
         auto *self = static_cast<ReadReplyFixture *>(arg);
         ++self->calls;
         for(uint16_t i = 0; i < self->registers_written; ++i) {
            (void)w.writeNumber<uint16_t, Endian::Big>(0x1234);
         }
         return self->result;
      },
      this);
      REQUIRE(slave.poll());
      return { txBuffer, txBuffer + writable.position() };
   }
};
} // namespace

TEST_CASE_FIXTURE(ReadReplyFixture, "[Modbus] Slave sends only an exception when the read callback fails after writing") {
   registers_written = 1;
   SUBCASE("address out of range") {
      result = ModbusSlave::Result::AddressOutOfRange;
      CHECK(poll(read_request(FunctionCode::ReadInputRegisters, 0, 2)) ==
            exception_frame(FunctionCode::ReadInputRegisters, ResultCode::ILLEGAL_ADDRESS));
   }
   SUBCASE("illegal value") {
      result = ModbusSlave::Result::IllegalValue;
      CHECK(poll(read_request(FunctionCode::ReadInputRegisters, 0, 2)) ==
            exception_frame(FunctionCode::ReadInputRegisters, ResultCode::ILLEGAL_VALUE));
   }
   SUBCASE("could not read") {
      result = ModbusSlave::Result::CouldNotRead;
      CHECK(poll(read_request(FunctionCode::ReadInputRegisters, 0, 2)) ==
            exception_frame(FunctionCode::ReadInputRegisters, ResultCode::FAILURE));
   }
}

TEST_CASE_FIXTURE(ReadReplyFixture, "[Modbus] Slave sends a failure when the read callback writes too little") {
   registers_written = 1;
   CHECK(poll(read_request(FunctionCode::ReadInputRegisters, 0, 2)) ==
         exception_frame(FunctionCode::ReadInputRegisters, ResultCode::FAILURE));
}

TEST_CASE_FIXTURE(ReadReplyFixture, "[Modbus] Slave refuses a read larger than its buffer") {
   registers_written = 5;
   CHECK(poll(read_request(FunctionCode::ReadInputRegisters, 0, 5)) ==
         exception_frame(FunctionCode::ReadInputRegisters, ResultCode::ILLEGAL_VALUE));
   CHECK(calls == 0);
}

TEST_CASE_FIXTURE(ReadReplyFixture, "[Modbus] Slave replies to a read that fills its buffer") {
   registers_written = 4;
   auto reply = poll(read_request(FunctionCode::ReadInputRegisters, 0, 4));
   REQUIRE(reply.size() == 3 + 8 + 2);
   CHECK(reply[2] == 8);
   uint8_t crc[2];
   compute_crc({ reply.data(), 11 }, crc);
   CHECK(reply[11] == crc[0]);
   CHECK(reply[12] == crc[1]);
}
