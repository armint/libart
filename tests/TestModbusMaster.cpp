
#include <catch2/catch_all.hpp>

#include <modbus/ModbusMaster.h>
#include <modbus/ModbusCodes.h>
#include <streams/ArrayReadable.h>
#include <streams/ArrayWritable.h>

using namespace modbus;

TEST_CASE("[Modbus] Master builds correct Read Input Registers frame", "[Modbus]") {
   // Arrange: readable with no data to force immediate timeout on response parsing
   uint8_t rxBuffer[1]{}; // empty/unused
   ArrayReadable readable({rxBuffer, 0});

   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   ModbusMaster master(readable, writable);
   master.setAddress(0x01);

   // We request to read 2 input registers (4 bytes) starting at address 0
   uint8_t responseData[4]{};

   // Act: perform the read; it will still write the request frame before failing to parse a response
   (void)master.readInputRegisters(0x0000, responseData);

   // Assert: verify the transmitted frame in txBuffer
   const size_t written = writable.position();
   REQUIRE(written == 8); // address(1) + function(1) + start(2) + count(2) + CRC(2)

   const uint8_t expected[] = {
      0x01, // address
      FunctionCode::ReadInputRegisters, // function = 0x04
      0x00, 0x00, // start address 0x0000 (big endian)
      0x00, 0x02, // quantity of registers = 2 (big endian)
      0x71, 0xCB  // CRC16-IBM (lo, hi)
   };

   REQUIRE(std::equal(std::begin(expected), std::end(expected), txBuffer));
}

TEST_CASE("[Modbus] Master builds correct Read Holding Registers frame", "[Modbus]") {
   // Arrange
   uint8_t rxBuffer[1]{}; // empty input to force timeout after write
   ArrayReadable readable({rxBuffer, 0});
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   ModbusMaster master(readable, writable);
   master.setAddress(0x01);

   uint8_t responseData[4]{}; // read 2 registers

   // Act
   (void)master.readHoldingRegisters(0x0000, responseData);

   // Assert
   const size_t written = writable.position();
   REQUIRE(written == 8);

   const uint8_t expected[] = {
      0x01, // address
      FunctionCode::ReadHoldingRegisters, // function = 0x03
      0x00, 0x00, // start address
      0x00, 0x02, // quantity = 2
      0xC4, 0x0B  // CRC16-IBM (lo, hi)
   };

   REQUIRE(std::equal(std::begin(expected), std::end(expected), txBuffer));
}

TEST_CASE("[Modbus] Master builds correct Write Single Register frame", "[Modbus]") {
   // Arrange
   uint8_t rxBuffer[1]{}; // empty input to force timeout after write
   ArrayReadable readable({rxBuffer, 0});
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);

   ModbusMaster master(readable, writable);
   master.setAddress(0x01);

   // Act: write 0x1234 at address 0x0001
   (void)master.writeSingleRegister(0x0001, 0x1234);
   constexpr uint8_t expected[] = {
      0x01, // address
      FunctionCode::WriteSingleHoldingRegister, // 0x06
      0x00, 0x01, // register address
      0x12, 0x34, // value (big-endian)
      0xD5, 0x7D  // CRC16-IBM (lo, hi)
   };


   // Assert
   const size_t written = writable.position();
   REQUIRE(written == std::size(expected)); // 8-byte RTU frame

   REQUIRE(std::equal(std::begin(expected), std::end(expected), txBuffer));
}

TEST_CASE("[Modbus] Master builds correct Write Multiple Registers frame", "[Modbus]") {
   // Arrange
   uint8_t rxBuffer[1]{}; // empty input to force timeout after write
   ArrayReadable readable({rxBuffer, 0});
   uint8_t txBuffer[64]{};
   ArrayWritable writable(txBuffer);

   ModbusMaster master(readable, writable);
   master.setAddress(0x01);

   // Two registers: 0x1122, 0x3344 (values must be provided big-endian in this API)
   const uint8_t values[4] = {0x11, 0x22, 0x33, 0x44};

   // Act
   (void)master.writeMultipleRegisters(0x0002, {values, 4});

   // Assert
   const size_t written = writable.position();
   REQUIRE(written == 13); // 1(addr)+1(fn)+2(start)+2(qty)+1(bytecnt)+4(data)+2(CRC) = 13

   const uint8_t expected[] = {
      0x01, // address
      FunctionCode::WriteMultipleHoldingRegisters, // 0x10
      0x00, 0x02, // start address
      0x00, 0x02, // quantity of registers = 2
      0x04,       // byte count
      0x11, 0x22, 0x33, 0x44, // data (big-endian per register)
      0xC3, 0x83  // CRC16-IBM (lo, hi)
   };

   REQUIRE(std::equal(std::begin(expected), std::end(expected), txBuffer));
}
