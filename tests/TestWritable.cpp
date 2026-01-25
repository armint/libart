#include <string>
#include <optional>
#include <doctest/doctest.h>

#include <streams/ArrayWritable.h>
#include <streams/Writable.h>
#include <utils/endian.h>

#include <cstdarg>
#include <vector>

// A small mock Writable to control flush behavior and optionally inject write errors
class MockWritable : public Writable {
   public:
   explicit MockWritable(std::span<uint8_t> buffer)
      : _buffer(buffer) {}

   // Configure write to fail after N successful bytes (optional)
   void failAfter(size_t nBytes, Error err) {
      _failAfter = nBytes;
      _failError = err;
   }

   void setFlushError(std::optional<Error> err) {
      _flushError = err;
   }

   std::expected<size_t, Error> write(std::span<const uint8_t> data) override {
      size_t written = 0;
      for(size_t i = 0; i < data.size(); ++i) {
         if(_position >= _buffer.size()) {
            return std::unexpected(Overflow);
         }
         if(_failAfter.has_value() && _bytesSucceeded >= *_failAfter) {
            return std::unexpected(_failError.value_or(CouldNotWrite));
         }
         _buffer[_position++] = data[i];
         ++written;
         ++_bytesSucceeded;
      }
      return written;
   }

   std::expected<void, Error> flush() override {
      if(_flushError.has_value()) {
         return std::unexpected(*_flushError);
      }
      return {};
   }

   std::span<uint8_t> span() {
      return _buffer.first(_position);
   }
   size_t position() const {
      return _position;
   }

   private:
   std::span<uint8_t> _buffer;
   size_t _position{ 0 };
   std::optional<size_t> _failAfter{};
   std::optional<Error> _failError{};
   size_t _bytesSucceeded{ 0 };
   std::optional<Error> _flushError{};
};

TEST_CASE("[Writable] write single byte and span [Writable]") {
   uint8_t buf[8]{};
   ArrayWritable w{ std::span<uint8_t>(buf) };

   // write single byte (call base overload explicitly to avoid name hiding)
   auto r1 = static_cast<Writable &>(w).write(static_cast<uint8_t>('A'));
   REQUIRE(r1);
   REQUIRE(r1.value() == 1);
   REQUIRE(buf[0] == 'A');

   // write a span
   const uint8_t more[] = { 'B', 'C', 'D' };
   auto r2 = w.write(std::span<const uint8_t>(more, sizeof(more)));
   REQUIRE(r2);
   REQUIRE(r2.value() == 3);
   REQUIRE(buf[1] == 'B');
   REQUIRE(buf[2] == 'C');
   REQUIRE(buf[3] == 'D');
}

TEST_CASE("[Writable] writeNumber endianness for integers [Writable]") {
   // Use a buffer large enough to test different sizes
   uint8_t buf[16]{};
   ArrayWritable w{ std::span<uint8_t>(buf) };

   // 16-bit
   {
      w.reset(0);
      uint16_t v = 0x1234;
      auto r = w.writeNumber<uint16_t, Endian::Little>(v);
      REQUIRE(r);
      REQUIRE(r.value() == sizeof(uint16_t));
      uint8_t expected[2];
      convertEndianToArray<Endian::Little, uint16_t>(v, static_cast<uint8_t *>(expected));
      REQUIRE(buf[0] == expected[0]);
      REQUIRE(buf[1] == expected[1]);
   }

   // 32-bit big
   {
      w.reset(0);
      uint32_t v = 0x89ABCDEFu;
      auto r = w.writeNumber<uint32_t, Endian::Big>(v);
      REQUIRE(r);
      REQUIRE(r.value() == sizeof(uint32_t));
      uint8_t expected[4];
      convertEndianToArray<Endian::Big, uint32_t>(v, static_cast<uint8_t *>(expected));
      for(size_t i = 0; i < 4; ++i) {
         REQUIRE(buf[i] == expected[i]);
      }
   }

   // 64-bit system endian (portable expectation via helper)
   {
      w.reset(0);
      uint64_t v = 0x0102030405060708ull;
      auto r = w.writeNumber<uint64_t, SystemEndian>(v);
      REQUIRE(r);
      REQUIRE(r.value() == sizeof(uint64_t));
      uint8_t expected[8];
      convertEndianToArray<SystemEndian, uint64_t>(v, static_cast<uint8_t *>(expected));
      for(size_t i = 0; i < 8; ++i) {
         REQUIRE(buf[i] == expected[i]);
      }
   }
}

TEST_CASE("[Writable] printf and println formatting and counts [Writable]") {
   uint8_t buf[128]{};
   MockWritable w{ std::span<uint8_t>(buf) };

   // printf basic formatting
   auto r = w.printf("Hello %s %d 0x%x", "World", -7, 0xABCD);
   REQUIRE(r);
   const char *expected = "Hello World -7 0xabcd"; // tfp_printf emits lowercase hex
   REQUIRE(r.value() == strlen(expected));
   REQUIRE(std::string(reinterpret_cast<char *>(buf), r.value()) == expected);

   // println without format (CRLF)
   auto r2 = w.println();
   REQUIRE(r2);
   REQUIRE(r2.value() == 2);
   REQUIRE(buf[r.value() + 0] == '\r');
   REQUIRE(buf[r.value() + 1] == '\n');

   // println with format appends CRLF and returns combined count
   size_t start = w.position();
   auto r3 = w.println("%s:%u", "val", 42u);
   REQUIRE(r3);
   const char *expectedLine = "val:42\r\n";
   REQUIRE(r3.value() == strlen(expectedLine));
   REQUIRE(std::string(reinterpret_cast<char *>(buf) + start, r3.value()) == expectedLine);
}

TEST_CASE("[Writable] printf_va path (va_list helper) [Writable]") {
   uint8_t buf[64]{};
   MockWritable w{ std::span<uint8_t>(buf) };

   // Use printf_va to drive the va_list-based implementation internally
   auto r = w.printf_va("%s %u", "abc", 99u);
   REQUIRE(r);
   REQUIRE(std::string(reinterpret_cast<char *>(buf), r.value()) == std::string("abc 99"));
}

TEST_CASE("[Writable] error propagation on overflow during printf [Writable]") {
   // Use a tiny buffer so that printf cannot fit
   uint8_t buf[4]{};
   ArrayWritable w{ std::span<uint8_t>(buf) };
   auto res = w.printf("This is too long %d", 12);
   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == Writable::Overflow);
}

TEST_CASE("[Writable] flush success and failure [Writable]") {
   uint8_t buf[8]{};
   MockWritable w{ std::span<uint8_t>(buf) };

   // success
   auto ok = w.flush();
   REQUIRE(ok.has_value());

   // failure
   w.setFlushError(Writable::CouldNotWrite);
   auto nok = w.flush();
   REQUIRE_FALSE(nok.has_value());
   REQUIRE(nok.error() == Writable::CouldNotWrite);
}
