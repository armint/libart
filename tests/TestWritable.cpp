#include <optional>
#include <doctest/doctest.h>
#include <string_view>

#include <streams/ArrayWritable.h>
#include <streams/Writable.h>
#include <utils/endian.h>

#include <cstdarg>

// A small mock Writable to control flush behavior and optionally inject write errors
class MockWritable : public Writable {
   public:
   explicit MockWritable(art::span<uint8_t> buffer)
      : _buffer(buffer) {}

   // Configure write to fail after N successful bytes (optional)
   void failAfter(size_t nBytes, Error err) {
      _failAfter = nBytes;
      _failError = err;
   }

   void setFlushError(std::optional<Error> err) {
      _flushError = err;
   }

   art::expected<size_t, Error> write(art::span<const uint8_t> data) override {
      size_t written = 0;
      for(size_t i = 0; i < data.size(); ++i) {
         if(_position >= _buffer.size()) {
            return art::unexpected(Overflow);
         }
         if(_failAfter.has_value() && _bytesSucceeded >= *_failAfter) {
            return art::unexpected(_failError.value_or(CouldNotWrite));
         }
         _buffer[_position++] = data[i];
         ++written;
         ++_bytesSucceeded;
      }
      return written;
   }

   art::expected<void, Error> flush() override {
      if(_flushError.has_value()) {
         return art::unexpected(*_flushError);
      }
      return {};
   }

   art::span<uint8_t> span() {
      return _buffer.first(_position);
   }
   size_t position() const {
      return _position;
   }

   private:
   art::span<uint8_t> _buffer;
   size_t _position{ 0 };
   std::optional<size_t> _failAfter{};
   std::optional<Error> _failError{};
   size_t _bytesSucceeded{ 0 };
   std::optional<Error> _flushError{};
};

TEST_CASE("[Writable] write single byte and span [Writable]") {
   uint8_t buf[8]{};
   ArrayWritable w{ art::span<uint8_t>(buf) };

   // write single byte (call base overload explicitly to avoid name hiding)
   auto r1 = static_cast<Writable &>(w).write(static_cast<uint8_t>('A'));
   REQUIRE(r1);
   REQUIRE(r1.value() == 1);
   REQUIRE(buf[0] == 'A');

   // write a span
   const uint8_t more[] = { 'B', 'C', 'D' };
   auto r2 = w.write(art::span<const uint8_t>(more, sizeof(more)));
   REQUIRE(r2);
   REQUIRE(r2.value() == 3);
   REQUIRE(buf[1] == 'B');
   REQUIRE(buf[2] == 'C');
   REQUIRE(buf[3] == 'D');
}

TEST_CASE("[Writable] writeNumber endianness for integers [Writable]") {
   // Use a buffer large enough to test different sizes
   uint8_t buf[16]{};
   ArrayWritable w{ art::span<uint8_t>(buf) };

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
   MockWritable w{ art::span<uint8_t>(buf) };

   // printf basic formatting
   auto r = w.printf("Hello %s %d 0x%x", "World", -7, 0xABCD);
   REQUIRE(r);
   const char *expected = "Hello World -7 0xabcd"; // tfp_printf emits lowercase hex
   REQUIRE(r.value() == strlen(expected));
   REQUIRE(std::string_view(reinterpret_cast<char *>(buf), r.value()) == expected);

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
   REQUIRE(std::string_view(reinterpret_cast<char *>(buf) + start, r3.value()) == expectedLine);
}

TEST_CASE("[Writable] printf_va path (va_list helper) [Writable]") {
   uint8_t buf[64]{};
   MockWritable w{ art::span<uint8_t>(buf) };

   // Use printf_va to drive the va_list-based implementation internally
   auto r = w.printf_va("%s %u", "abc", 99u);
   REQUIRE(r);
   REQUIRE(std::string_view(reinterpret_cast<char *>(buf), r.value()) == "abc 99");
}

TEST_CASE("[Writable] error propagation on overflow during printf [Writable]") {
   // Use a tiny buffer so that printf cannot fit
   uint8_t buf[4]{};
   ArrayWritable w{ art::span<uint8_t>(buf) };
   auto res = w.printf("This is too long %d", 12);
   REQUIRE_FALSE(res.has_value());
   REQUIRE(res.error() == Writable::Overflow);
}

// A Writable that records begin/end write session calls, to verify session bracketing.
class SessionTrackingWritable : public Writable {
   public:
   explicit SessionTrackingWritable(art::span<uint8_t> buffer)
      : _buffer(buffer) {}

   art::expected<size_t, Error> write(art::span<const uint8_t> data) override {
      size_t written = 0;
      for(size_t i = 0; i < data.size() && _position < _buffer.size(); ++i, ++_position, ++written) {
         _buffer[_position] = data[i];
      }
      if(written != data.size()) {
         return art::unexpected(Overflow);
      }
      return written;
   }

   art::expected<void, Error> flush() override {
      return {};
   }

   int beginCount = 0;
   int endCount = 0;

   protected:
   void beginWriteSession() noexcept override {
      ++beginCount;
   }
   void endWriteSession() noexcept override {
      ++endCount;
   }

   private:
   art::span<uint8_t> _buffer;
   size_t _position{ 0 };
};

TEST_CASE("[Writable] default beginWriteSession/endWriteSession are no-ops [Writable]") {
   // A sink that does not override the hooks (MockWritable) must behave exactly as before:
   // printf/println still work, and there is nothing to observe about session state.
   uint8_t buf[64]{};
   MockWritable w{ art::span<uint8_t>(buf) };
   auto r = w.printf("%s=%d", "x", 5);
   REQUIRE(r);
   REQUIRE(std::string_view(reinterpret_cast<char *>(buf), r.value()) == "x=5");
}

TEST_CASE("[Writable] vprintf (via printf) opens exactly one session per call [Writable]") {
   uint8_t buf[64]{};
   SessionTrackingWritable w{ art::span<uint8_t>(buf) };

   auto r = w.printf("%s", "hi");
   REQUIRE(r);
   CHECK(w.beginCount == 1);
   CHECK(w.endCount == 1);

   auto r2 = w.println("bye");
   REQUIRE(r2);
   // println() = printf() + write(), so only the printf() call opens a session.
   CHECK(w.beginCount == 2);
   CHECK(w.endCount == 2);
}

TEST_CASE("[Writable] a caller-held writeSession() stays open across multiple printf() calls [Writable]") {
   uint8_t buf[64]{};
   SessionTrackingWritable w{ art::span<uint8_t>(buf) };

   {
      auto outer = w.writeSession();
      CHECK(w.beginCount == 1);
      CHECK(w.endCount == 0);

      (void)w.printf("a");
      (void)w.printf("b");
      // Nested sessions from printf()'s own vprintf() each begin and end in turn...
      CHECK(w.beginCount == 3);
      CHECK(w.endCount == 2);
      // ...while the outer session is still open.
   }
   // Outer session closes once it goes out of scope.
   CHECK(w.beginCount == 3);
   CHECK(w.endCount == 3);
}

TEST_CASE("[Writable] writeSession() is move-only and end fires exactly once [Writable]") {
   uint8_t buf[16]{};
   SessionTrackingWritable w{ art::span<uint8_t>(buf) };

   {
      auto s1 = w.writeSession();
      CHECK(w.beginCount == 1);
      auto s2 = std::move(s1);
      // Moving must not run the end hook.
      CHECK(w.endCount == 0);
   }
   // Only the moved-to object's destructor ends the session.
   CHECK(w.beginCount == 1);
   CHECK(w.endCount == 1);
}

TEST_CASE("[Writable] flush success and failure [Writable]") {
   uint8_t buf[8]{};
   MockWritable w{ art::span<uint8_t>(buf) };

   // success
   auto ok = w.flush();
   REQUIRE(ok.has_value());

   // failure
   w.setFlushError(Writable::CouldNotWrite);
   auto nok = w.flush();
   REQUIRE_FALSE(nok.has_value());
   REQUIRE(nok.error() == Writable::CouldNotWrite);
}
