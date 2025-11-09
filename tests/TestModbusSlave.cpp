#include <catch2/catch_all.hpp>

#include <modbus/ModbusSlave.h>
#include <modbus/ModbusCodes.h>
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
   const uint8_t request[] = {0x01, FunctionCode::ReadHoldingRegisters, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
   ArrayReadable readable({const_cast<uint8_t*>(request), sizeof(request)});

   // Capture the slave response
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   // Modbus slave with small temp buffer (not used for reads)
   uint16_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, std::span<uint16_t>(tmpBuf));
   slave.setAddress(0x01);

   // Provide a read-holding callback that writes two registers: 0x1122 and 0x3344
   slave.setReadHoldingCallback([](uint16_t address, uint16_t count, Writable& w) -> bool {
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
   const uint8_t expectedPayload[] = {0x11, 0x22, 0x33, 0x44};
   REQUIRE(std::equal(std::begin(expectedPayload), std::end(expectedPayload), &txBuffer[3]));

   // Verify CRC independently
   uint8_t crc[2]{};
   compute_crc({txBuffer, 7}, crc); // compute over addr..data
   REQUIRE(txBuffer[7] == crc[0]); // CRC lo
   REQUIRE(txBuffer[8] == crc[1]); // CRC hi
}

TEST_CASE("[Modbus] Slave replies to Read Holding Registers (float)", "[Modbus]") {
   // Build a RTU request: addr=0x01, fn=0x03, start=0x0000, qty=0x0002, CRC=0x0B after 0xC4 (lo,hi)
   const uint8_t request[] = {0x01, FunctionCode::ReadHoldingRegisters, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
   ArrayReadable readable({const_cast<uint8_t*>(request), sizeof(request)});

   // Capture the slave response
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   // Modbus slave with small temp buffer (not used for reads)
   uint16_t tmpBuf[4]{};
   ModbusSlave slave(readable, writable, std::span<uint16_t>(tmpBuf));
   slave.setAddress(0x01);

   // Provide a read-holding callback that writes two registers: 0x1122 and 0x3344
   slave.setReadHoldingCallback([](uint16_t address, uint16_t count, Writable& w) -> bool {
      REQUIRE(address == 0x0000);
      REQUIRE(count == 2);
      // Two registers, big-endian per register
      float f = 230.2f;
      w.writeNumber<float, Endian::Big>(f);
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
   const uint8_t expectedPayload[] = {0x43, 0x66, 0x33, 0x34};
   REQUIRE(std::equal(std::begin(expectedPayload), std::end(expectedPayload), &txBuffer[3]));

   // Verify CRC independently
   uint8_t crc[2]{};
   compute_crc({txBuffer, 7}, crc); // compute over addr..data
   REQUIRE(txBuffer[7] == crc[0]); // CRC lo
   REQUIRE(txBuffer[8] == crc[1]); // CRC hi
}
