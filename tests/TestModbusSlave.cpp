#include "modbus/ModbusMaster.h"


#include <catch2/catch_all.hpp>

#include <modbus/ModbusCodes.h>
#include <modbus/ModbusSlave.h>
#include <streams/ArrayReadable.h>
#include <streams/ArrayWritable.h>
#include <utils/IBMCRC16Digest.h>

using namespace modbus;

static void compute_crc(std::span<const uint8_t> frame_wo_crc, uint8_t (&crc)[2]) {
   IBMCRC16Digest d;
   d.update(frame_wo_crc);
   (void)d.getDigest(crc);
}

TEST_CASE("[Modbus] Slave replies to Read Holding Registers (2 regs)", "[Modbus]") {
   // Build a RTU request: addr=0x01, fn=0x03, start=0x0000, qty=0x0002, CRC=0x0B after 0xC4 (lo,hi)
   const uint8_t request[] = { 0x01, FunctionCode::ReadHoldingRegisters, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B };
   ArrayReadable readable({ const_cast<uint8_t *>(request), sizeof(request) });

   // Capture the slave response
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   // Modbus slave with small temp buffer (not used for reads)
   uint8_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);

   // Provide a read-holding callback that writes two registers: 0x1122 and 0x3344
   slave.setReadHoldingCallback([](uint16_t address, uint16_t count, Writable &w) -> bool {
      REQUIRE(address == 0x0000);
      REQUIRE(count == 2);
      // Two registers, big-endian per register
      (void)w.writeNumber<uint16_t, Endian::Big>(0x1122);
      (void)w.writeNumber<uint16_t, Endian::Big>(0x3344);
      return true; // address accepted
   });

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

TEST_CASE("[Modbus] Slave replies to Read Holding Registers (float)", "[Modbus]") {
   // Build a RTU request: addr=0x01, fn=0x03, start=0x0000, qty=0x0002, CRC=0x0B after 0xC4 (lo,hi)
   const uint8_t request[] = { 0x01, FunctionCode::ReadInputRegisters, 0x00, 0x00, 0x00, 0x02, 0x71, 0xCB };
   ArrayReadable readable({ const_cast<uint8_t *>(request), sizeof(request) });
   const float number = 230.2f;

   // Capture the slave response
   uint8_t slaveTxBuffer[32]{};
   ArrayWritable writable(slaveTxBuffer);

   // Modbus slave with small temp buffer (not used for reads)
   uint8_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);

   slave.setReadInputCallback([number](uint16_t address, uint16_t count, Writable &w) -> bool {
      REQUIRE(address == 0x0000);
      REQUIRE(count == 2);
      // Two registers, big-endian per register
      w.writeNumber<float>(number);
      return true; // address accepted
   });

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
   ArrayReadable masterReadable({ std::span<const uint8_t>(slaveTxBuffer, 9) });
   ArrayWritable masterWritable(masterTxBuffer);
   ModbusMaster master(masterReadable, masterWritable);
   uint8_t masterOut[4];
   auto masterResult = master.readInputRegisters(1, 0, masterOut);
   REQUIRE(masterResult);
   REQUIRE(masterResult.value() == 4);
   float floatValue;
   std::memcpy(&floatValue, masterOut, sizeof(float));
   REQUIRE(floatValue == Catch::Approx(230.2f));
}

TEST_CASE("[Modbus] Slave processes Write Multiple Holding Registers and replies", "[Modbus]") {
   // Build a RTU request for function 0x10:
   // addr=0x01, fn=0x10, start=0x0002, qty=0x0002, bytecnt=0x04, data=0x11 0x22 0x33 0x44
   uint8_t frame[13]{};
   frame[0] = 0x01;
   frame[1] = FunctionCode::WriteMultipleHoldingRegisters;
   frame[2] = 0x00; frame[3] = 0x02; // start
   frame[4] = 0x00; frame[5] = 0x02; // qty
   frame[6] = 0x04;                  // byte count
   frame[7] = 0x11; frame[8] = 0x22; frame[9] = 0x33; frame[10] = 0x44; // data
   {
      uint8_t crc[2]{};
      compute_crc({frame, 11}, crc); // compute CRC over addr..data
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
   slave.setWriteHoldingCallback([&](uint16_t address, BoundedReadable &r) -> bool {
      callbackCalled = true;
      REQUIRE(address == 0x0002);
      // Expect exactly 4 bytes for 2 registers
      uint8_t buf[4]{};
      auto res = r.read(buf);
      REQUIRE(res);
      REQUIRE(res.value() == 4);
      const uint8_t expectedData[] = {0x11, 0x22, 0x33, 0x44};
      REQUIRE(std::equal(std::begin(expectedData), std::end(expectedData), buf));
      return true; // accept write
   });

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
   compute_crc({txBuffer, 6}, crc);
   REQUIRE(txBuffer[6] == crc[0]);
   REQUIRE(txBuffer[7] == crc[1]);
}

TEST_CASE("[Modbus] Slave processes broadcast Write Multiple without replying", "[Modbus]") {
   // Broadcast address 0x00, same payload as previous test
   uint8_t frame[13]{};
   frame[0] = 0x00; // broadcast
   frame[1] = FunctionCode::WriteMultipleHoldingRegisters;
   frame[2] = 0x00; frame[3] = 0x02;
   frame[4] = 0x00; frame[5] = 0x02;
   frame[6] = 0x04;
   frame[7] = 0xAA; frame[8] = 0xBB; frame[9] = 0xCC; frame[10] = 0xDD;
   {
      uint8_t crc[2]{};
      compute_crc({frame, 11}, crc);
      frame[11] = crc[0];
      frame[12] = crc[1];
   }

   ArrayReadable readable({frame, sizeof(frame)});
   uint8_t txBuffer[16]{};
   ArrayWritable writable(txBuffer);

   uint8_t tmpBuf[8]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01); // our slave address (ignored due to broadcast)

   bool callbackCalled = false;
   slave.setWriteHoldingCallback([&](uint16_t address, BoundedReadable &r) -> bool {
      callbackCalled = true;
      REQUIRE(address == 0x0002);
      uint8_t data[4]{};
      auto res = r.read(data);
      REQUIRE(res);
      REQUIRE(res.value() == 4);
      const uint8_t expected[] = {0xAA, 0xBB, 0xCC, 0xDD};
      REQUIRE(std::equal(std::begin(expected), std::end(expected), data));
      return true;
   });

   const bool processed = slave.poll();
   REQUIRE(processed);
   REQUIRE(callbackCalled);
   // Since broadcast, there must be no response
   REQUIRE(writable.position() == 0);
}

TEST_CASE("[Modbus] Slave returns exception on write callback rejection", "[Modbus]") {
   // Valid request, but application refuses (returns false) -> expect ILLEGAL_ADDRESS exception
   uint8_t frame[13]{};
   frame[0] = 0x01;
   frame[1] = FunctionCode::WriteMultipleHoldingRegisters;
   frame[2] = 0x00; frame[3] = 0x10; // start
   frame[4] = 0x00; frame[5] = 0x01; // qty=1
   frame[6] = 0x02;                  // byte count
   frame[7] = 0x12; frame[8] = 0x34; // data
   // pad remaining byte to keep array size, though not used
   {
      uint8_t crc[2]{};
      compute_crc({frame, 9}, crc);
      frame[9] = crc[0];
      frame[10] = crc[1];
   }

   ArrayReadable readable({frame, 11}); // only up to CRC
   uint8_t txBuffer[16]{};
   ArrayWritable writable(txBuffer);

   uint8_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, tmpBuf);
   slave.setAddress(0x01);

   slave.setWriteHoldingCallback([&](uint16_t, BoundedReadable &) -> bool { return false; });

   REQUIRE(slave.poll());

   // Expect exception frame: addr, fn|0x80, code, CRC (total 5 bytes)
   REQUIRE(writable.position() == 5);
   REQUIRE(txBuffer[0] == 0x01);
   REQUIRE(txBuffer[1] == (FunctionCode::WriteMultipleHoldingRegisters | 0x80));
   REQUIRE(txBuffer[2] == ResultCode::ILLEGAL_ADDRESS);
   uint8_t crc[2]{};
   compute_crc({txBuffer, 3}, crc);
   REQUIRE(txBuffer[3] == crc[0]);
   REQUIRE(txBuffer[4] == crc[1]);
}

TEST_CASE("[Modbus] Slave returns exception on invalid byte count", "[Modbus]") {
   // Malformed: qty=2 but bytecount=3 (should be 4) -> expect ILLEGAL_VALUE
   uint8_t frame[12]{};
   frame[0] = 0x01;
   frame[1] = FunctionCode::WriteMultipleHoldingRegisters;
   frame[2] = 0x00; frame[3] = 0x02; // start
   frame[4] = 0x00; frame[5] = 0x02; // qty=2
   frame[6] = 0x03;                  // wrong byte count
   frame[7] = 0xBE; frame[8] = 0xEF; frame[9] = 0x01; // only 3 data bytes
   {
      uint8_t crc[2]{};
      compute_crc({frame, 10}, crc);
      frame[10] = crc[0];
      frame[11] = crc[1];
   }

   ArrayReadable readable({frame, sizeof(frame)});
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
   compute_crc({txBuffer, 3}, crc);
   REQUIRE(txBuffer[3] == crc[0]);
   REQUIRE(txBuffer[4] == crc[1]);
}
