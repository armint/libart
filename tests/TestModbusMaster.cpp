
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
   // We request to read 2 input registers (4 bytes) starting at address 0
   uint8_t responseData[4]{};

   // Act: perform the read; it will still write the request frame before failing to parse a response
   (void)master.readInputRegisters(0x01, 0x0000, responseData);

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

   uint8_t responseData[4]{}; // read 2 registers

   // Act
   (void)master.readHoldingRegisters(0x01, 0x0000, responseData);

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

   // Act: write 0x1234 at address 0x0001
   (void)master.writeSingleRegister(0x01, 0x0001, 0x1234);
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

TEST_CASE("[Modbus] Master builds correct Write Multiple Registers frame and parses response", "[Modbus]") {
   // Arrange
   // Provided response for function 0x10 (addr=0x01, start=0x0002, qty=0x0002, CRC=0x08E0 [lo,hi=E0,08])
   const uint8_t rxData[] = {0x01, 0x10, 0x00, 0x02, 0x00, 0x02, 0xE0, 0x08};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[64]{};
   ArrayWritable writable(txBuffer);

   ModbusMaster master(readable, writable);

   // Two registers: 0x1122, 0x3344 (values must be provided big-endian in this API)
   const uint8_t values[4] = {0x11, 0x22, 0x33, 0x44};

   // Act
   auto res = master.writeMultipleRegisters(0x01, 0x0002, {values, 4});

   // Assert (TX frame correctness)
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

   // And (RX response parsing)
   REQUIRE(res.has_value());
   REQUIRE(res.value() == 4); // response payload length for 0x10 is 4 (start addr + qty)
}

TEST_CASE("[Modbus] Master parses given Read Input Registers response", "[Modbus]") {
   // Provided response bytes (address, function, byte count, data[4], CRC lo, CRC hi)
   const uint8_t rxData[] = {0x01, 0x04, 0x04, 0x43, 0x66, 0x33, 0x34, 0x1B, 0x38};
   ArrayReadable readable(rxData);

   uint8_t txBuffer[32]{}; // capture the request frame (not asserted here)
   ArrayWritable writable(txBuffer);

   ModbusMaster master(readable, writable);

   uint8_t out[4]{}; // expect to read 4 data bytes (2 input registers)
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE(res.has_value());
   REQUIRE(res.value() == 4);

   const uint8_t expectedPayload[] = {0x43, 0x66, 0x33, 0x34};

   REQUIRE(std::equal(std::begin(expectedPayload), std::end(expectedPayload), out));

   // Convert payload from big-endian to float and verify it's 230.2
   uint8_t floatBytes[4] = {out[3], out[2], out[1], out[0]};
   float floatValue;
   std::memcpy(&floatValue, floatBytes, sizeof(float));
   REQUIRE(floatValue == Catch::Approx(230.2f));
}

TEST_CASE("[Modbus] Master parses Read Input Registers response with 1 register", "[Modbus]") {
   // Address 1, Function 4, Byte count 2, Data 0x1234, CRC
   const uint8_t rxData[] = {0x01, 0x04, 0x02, 0x12, 0x34, 0xB4, 0x47};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE(res.has_value());
   REQUIRE(res.value() == 2);
   REQUIRE(out[0] == 0x12);
   REQUIRE(out[1] == 0x34);
}

TEST_CASE("[Modbus] Master readCoils works", "[Modbus]") {
   // Address 1, Function 1, Byte count 2, Data 0xCD, 0x01, CRC
   const uint8_t rxData[] = {0x01, 0x01, 0x02, 0xCD, 0x01, 0x2C, 0xAC};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readCoils(0x01, 0x0000, 10, out);

   REQUIRE(res.has_value());
   REQUIRE(res.value() == 2);
   REQUIRE(out[0] == 0xCD);
   REQUIRE(out[1] == 0x01);

   // Verify TX
   const size_t written = writable.position();
   REQUIRE(written == 8);
   const uint8_t expectedTx[] = {0x01, 0x01, 0x00, 0x00, 0x00, 0x0A, 0xBC, 0x0D};
   REQUIRE(std::equal(std::begin(expectedTx), std::end(expectedTx), txBuffer));
}

TEST_CASE("[Modbus] Master writeSingleCoil works", "[Modbus]") {
   // Address 1, Function 5, Coil Addr 0x00AC, Value 0xFF00, CRC
   const uint8_t rxData[] = {0x01, 0x05, 0x00, 0xAC, 0xFF, 0x00, 0x4C, 0x1B};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   auto res = master.writeSingleCoil(0x01, 0x00AC, true);

   REQUIRE(res.has_value());
   REQUIRE(res.value() == 4);

   // Verify TX
   const size_t written = writable.position();
   REQUIRE(written == 8);
   const uint8_t expectedTx[] = {0x01, 0x05, 0x00, 0xAC, 0xFF, 0x00, 0x4C, 0x1B};
   REQUIRE(std::equal(std::begin(expectedTx), std::end(expectedTx), txBuffer));
}

TEST_CASE("[Modbus] Master writeMultipleCoils works", "[Modbus]") {
   // Address 1, Function 15 (0x0F), Start 0x0013, Count 10, CRC
   const uint8_t rxData[] = {0x01, 0x0F, 0x00, 0x13, 0x00, 0x0A, 0x24, 0x09};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   const uint8_t values[] = {0xCD, 0x01};
   auto res = master.writeMultipleCoils(0x01, 0x0013, 10, values);

   REQUIRE(res.has_value());
   REQUIRE(res.value() == 4);

   // Verify TX
   const size_t written = writable.position();
   REQUIRE(written == 11); // 1+1+2+2+1+2+2 = 11
   const uint8_t expectedTx[] = {0x01, 0x0F, 0x00, 0x13, 0x00, 0x0A, 0x02, 0xCD, 0x01, 0x72, 0xCB};
   REQUIRE(std::equal(std::begin(expectedTx), std::end(expectedTx), txBuffer));
}

TEST_CASE("[Modbus] Master readDiscreteInputs works", "[Modbus]") {
   // Address 1, Function 2, Byte count 3, Data 0xAC, 0xDB, 0x35, CRC
   const uint8_t rxData[] = {0x01, 0x02, 0x03, 0xAC, 0xDB, 0x35, 0x22, 0x88};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[3]{};
   auto res = master.readDiscreteInputs(0x01, 0x00C4, 22, out);

   REQUIRE(res.has_value());
   REQUIRE(res.value() == 3);
   REQUIRE(out[0] == 0xAC);
   REQUIRE(out[1] == 0xDB);
   REQUIRE(out[2] == 0x35);

   // Verify TX
   const size_t written = writable.position();
   REQUIRE(written == 8);
   const uint8_t expectedTx[] = {0x01, 0x02, 0x00, 0xC4, 0x00, 0x16, 0xB8, 0x39};
   REQUIRE(std::equal(std::begin(expectedTx), std::end(expectedTx), txBuffer));
}

TEST_CASE("[Modbus] Master detects address mismatch", "[Modbus]") {
   // Expected address 1, but received address 2
   const uint8_t rxData[] = {0x02, 0x04, 0x02, 0x12, 0x34, 0xBD, 0x07};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == ResponseStatus::AddressMismatch);
}

TEST_CASE("[Modbus] Master detects function code mismatch", "[Modbus]") {
   // Requested function 4, but received function 3
   const uint8_t rxData[] = {0x01, 0x03, 0x02, 0x12, 0x34, 0x44, 0x3E};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == ResponseStatus::FunctionCodeMismatch);
}

TEST_CASE("[Modbus] Master detects CRC mismatch", "[Modbus]") {
   // Correct response would have CRC B4 47, but we provide 00 00
   const uint8_t rxData[] = {0x01, 0x04, 0x02, 0x12, 0x34, 0x00, 0x00};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == ResponseStatus::CRCMismatch);
}

TEST_CASE("[Modbus] Master detects exception response", "[Modbus]") {
   // Address 1, Function 0x84 (0x04 | 0x80), Exception Code 0x02 (Illegal Data Address), CRC
   const uint8_t rxData[] = {0x01, 0x84, 0x02, 0xC2, 0xC1};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == ResponseStatus::FunctionCodeException);
   REQUIRE(master.getExceptionCode() == 0x02);
}

TEST_CASE("[Modbus] Master detects read timeout", "[Modbus]") {
   // Provide only part of the response
   const uint8_t rxData[] = {0x01, 0x04};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == ResponseStatus::ReadTimeout);
}

TEST_CASE("[Modbus] Master detects buffer overflow", "[Modbus]") {
   // Response says 4 bytes, but we only provided 2-byte buffer
   const uint8_t rxData[] = {0x01, 0x04, 0x04, 0x11, 0x22, 0x33, 0x44, 0x2B, 0x5A};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == ResponseStatus::BufferOverflow);
}

TEST_CASE("[Modbus] Master detects read timeout after byte count", "[Modbus]") {
   // Address 1, Function 4, Byte count 2, but no data
   const uint8_t rxData[] = {0x01, 0x04, 0x02};
   ArrayReadable readable(rxData);
   uint8_t txBuffer[32]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == ResponseStatus::ReadTimeout);
}

class FailingWritable : public Writable {
   public:
   std::expected<size_t, Error> write(std::span<const uint8_t>) override {
      return std::unexpected(CouldNotWrite);
   }
   std::expected<void, Error> flush() override {
      return {};
   }
};

TEST_CASE("[Modbus] Master detects write error", "[Modbus]") {
   uint8_t rxData[] = {0x01, 0x04, 0x02, 0x12, 0x34, 0xB4, 0x47};
   ArrayReadable readable(rxData);
   FailingWritable writable;
   ModbusMaster master(readable, writable);

   uint8_t out[2]{};
   auto res = master.readInputRegisters(0x01, 0x0000, out);

   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == ResponseStatus::WriteError);
}

TEST_CASE("[Modbus] Master broadcast does not expect response", "[Modbus]") {
   // No data in readable
   uint8_t rxBuffer[1]{};
   ArrayReadable readable({rxBuffer, 0});
   uint8_t txBuffer[64]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   // Act
   auto res = master.writeSingleRegister(0x00, 0x0001, 0x1234);

   // Assert
   REQUIRE(res.has_value());
   REQUIRE(res.value() == 0);

   // Verify TX frame still happened (Address 0, Function 6, Reg 0x0001, Val 0x1234, CRC)
   REQUIRE(writable.position() == 8);
   const uint8_t expectedTx[] = {0x00, 0x06, 0x00, 0x01, 0x12, 0x34, 0xD4, 0xAC};
   REQUIRE(std::equal(std::begin(expectedTx), std::end(expectedTx), txBuffer));
}

class DynamicReadable : public Readable {
   public:
   std::span<const uint8_t> data;
   size_t pos = 0;
   std::expected<char, Error> read() override {
      if(pos < data.size()) {
         return static_cast<char>(data[pos++]);
      }
      return std::unexpected(Timeout);
   }
   bool available() override {
      return pos < data.size();
   }
};

TEST_CASE("[Modbus] Master exception code behavior", "[Modbus]") {
   DynamicReadable readable;
   uint8_t txBuffer[64]{};
   ArrayWritable writable(txBuffer);
   ModbusMaster master(readable, writable);

   // 1. Trigger an exception
   const uint8_t rxData1[] = {0x01, 0x84, 0x02, 0xC2, 0xC1};
   readable.data = rxData1;
   readable.pos = 0;
   auto res1 = master.readInputRegisters(0x01, 0x0000, {});
   REQUIRE_FALSE(res1.has_value());
   REQUIRE(master.getExceptionCode() == 0x02);

   // 2. Trigger a success and see if exception code is still there (current behavior)
   const uint8_t rxData2[] = {0x01, 0x04, 0x02, 0x12, 0x34, 0xB4, 0x47};
   readable.data = rxData2;
   readable.pos = 0;
   uint8_t out[2]{};
   auto res2 = master.readInputRegisters(0x01, 0x0000, out);
   REQUIRE(res2.has_value());
   // Verify that exception code is cleared on a new successful request
   REQUIRE(master.getExceptionCode() == 0x00);
}
