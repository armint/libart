#include "RamBlockDevice.h"
#include "storage/BlockDeviceDecorator.h"
#include "storage/littlefs/LittleFsFileSystem.h"
#include "streams/BufferedWritable.h"
#include "streams/DigestReadable.h"
#include "streams/FileReadable.h"
#include "streams/FileWritable.h"
#include "utils/SHA256Digest.h"
#include <array>
#include <doctest/doctest.h>
#include <optional>
#include <vector>

// FileReadable and FileWritable are tested against the real littlefs, through LittleFsFileSystem over a
// RamBlockDevice, because they must behave the same on any FileSystem and littlefs is the one that ships.

using namespace art::storage;
using namespace art::storage::test;

namespace {

// NOR-like: 16 byte read and program unit, 4 KiB erase unit, 16 blocks.
// clang-format off
constexpr Geometry kGeometry{
   .readSize = 16u, .programSize = 16u, .eraseSize = 4096u, .size = 64u * 1024u, .eraseBeforeProgram = true
};
// clang-format on

// Fails every read with a chosen error while failWith is set.
class FailingReadDevice final : public BlockDeviceDecorator {
   public:
   using BlockDeviceDecorator::BlockDeviceDecorator;

   std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) override {
      if(failWith) {
         return std::unexpected(*failWith);
      }
      return BlockDeviceDecorator::read(offset, buffer);
   }

   std::optional<Error> failWith;
};

struct Fixture {
   Fixture()
      : ram(kGeometry, backing)
      , failing(ram)
      , fileSystem(failing) {
      REQUIRE(fileSystem.mount().has_value());
   }

   // A recognisable, non-repeating pattern so a misplaced or duplicated byte shows up in a comparison.
   static std::vector<uint8_t> pattern(size_t size) {
      std::vector<uint8_t> data(size);
      for(size_t i = 0; i < size; ++i) {
         data[i] = static_cast<uint8_t>((i * 7u) ^ (i >> 8u));
      }
      return data;
   }

   File open(const char *path, OpenMode mode) {
      auto file = fileSystem.open(path, mode);
      REQUIRE(file.has_value());
      return std::move(*file);
   }

   std::vector<uint8_t> stage(const char *path, size_t size) {
      auto data = pattern(size);
      REQUIRE(fileSystem.writeFile(path, data).has_value());
      return data;
   }

   std::array<uint8_t, 64u * 1024u> backing{};
   RamBlockDevice ram;
   FailingReadDevice failing;
   LittleFsFileSystem<> fileSystem;
};

std::array<uint8_t, 32> sha256Of(std::span<const uint8_t> data) {
   SHA256Digest digest;
   digest.update(data);
   std::array<uint8_t, 32> hash{};
   REQUIRE(digest.getDigest(hash).has_value());
   return hash;
}

} // namespace

TEST_SUITE("FileReadable and FileWritable over LittleFsFileSystem") {

   TEST_CASE("a file is hashed through DigestReadable without the stream knowing the file system") {
      Fixture fixture;
      // More than one erase unit, and not a multiple of the stream buffer, so refills and a short last one occur.
      const auto data = fixture.stage("/image", 9000u);
      File file = fixture.open("/image", OpenMode::Read);
      std::array<uint8_t, 100> buffer{};
      FileReadable readable(file, buffer);
      SHA256Digest digest;
      DigestReadable hashing(readable, digest);

      std::vector<uint8_t> received(data.size());
      const auto count = hashing.read(std::span<uint8_t>(received));
      REQUIRE(count.has_value());
      CHECK(*count == data.size());
      CHECK(received == data);
      CHECK_FALSE(hashing.available());

      std::array<uint8_t, 32> hash{};
      REQUIRE(digest.getDigest(hash).has_value());
      CHECK(hash == sha256Of(data));
   }

   TEST_CASE("FileReadable reads to the end and then reports EndOfStream") {
      Fixture fixture;
      const auto data = fixture.stage("/f", 10u);
      File file = fixture.open("/f", OpenMode::Read);
      std::array<uint8_t, 4> buffer{};
      FileReadable readable(file, buffer);

      CHECK(readable.bytesLeft() == 10u);
      for(const uint8_t expected : data) {
         const auto byte = readable.read();
         REQUIRE(byte.has_value());
         CHECK(static_cast<uint8_t>(*byte) == expected);
      }
      const auto atEnd = readable.read();
      REQUIRE_FALSE(atEnd.has_value());
      CHECK(atEnd.error() == Readable::EndOfStream);
      CHECK_FALSE(readable.available());
      CHECK(readable.bytesLeft() == 0u);
      CHECK(readable.position() == 10u);
      CHECK_FALSE(readable.lastError().has_value()); // the end of a file is not an error
   }

   TEST_CASE("FileReadable of an empty file is at its end immediately") {
      Fixture fixture;
      fixture.stage("/empty", 0u);
      File file = fixture.open("/empty", OpenMode::Read);
      std::array<uint8_t, 4> buffer{};
      FileReadable readable(file, buffer);
      CHECK_FALSE(readable.available());
      CHECK(readable.read().error() == Readable::EndOfStream);
   }

   TEST_CASE("FileReadable position, bytesLeft and available follow the reads, not the buffer fill") {
      Fixture fixture;
      fixture.stage("/f", 10u);
      File file = fixture.open("/f", OpenMode::Read);
      std::array<uint8_t, 4> buffer{};
      FileReadable readable(file, buffer);

      REQUIRE(readable.read().has_value());
      CHECK(*file.tell() == 4u); // the buffer was filled ahead
      CHECK(readable.position() == 1u);
      CHECK(readable.bytesLeft() == 9u);
      CHECK(readable.available());
   }

   TEST_CASE("FileReadable starts at the current position of the file") {
      Fixture fixture;
      const auto data = fixture.stage("/f", 10u);
      File file = fixture.open("/f", OpenMode::Read);
      REQUIRE(file.seek(6u).has_value());
      std::array<uint8_t, 4> buffer{};
      FileReadable readable(file, buffer);
      CHECK(readable.position() == 6u);
      CHECK(readable.bytesLeft() == 4u);
      CHECK(static_cast<uint8_t>(*readable.read()) == data[6]);
   }

   TEST_CASE("FileReadable reset inside the buffer does not touch the file, outside it seeks") {
      Fixture fixture;
      const auto data = fixture.stage("/f", 20u);
      File file = fixture.open("/f", OpenMode::Read);
      std::array<uint8_t, 8> buffer{};
      FileReadable readable(file, buffer);
      REQUIRE(readable.read().has_value()); // buffers bytes 0..7

      readable.reset(5u);
      CHECK(*file.tell() == 8u); // still the buffer fill: no seek
      CHECK(readable.position() == 5u);
      CHECK(static_cast<uint8_t>(*readable.read()) == data[5]);

      readable.reset(15u);
      CHECK(readable.position() == 15u);
      CHECK(static_cast<uint8_t>(*readable.read()) == data[15]);

      readable.reset(); // default: rewind
      CHECK(readable.position() == 0u);
      CHECK(readable.bytesLeft() == 20u);
      CHECK(static_cast<uint8_t>(*readable.read()) == data[0]);
   }

   TEST_CASE("FileReadable reset beyond the end is allowed and reads report EndOfStream") {
      Fixture fixture;
      fixture.stage("/f", 4u);
      File file = fixture.open("/f", OpenMode::Read);
      std::array<uint8_t, 4> buffer{};
      FileReadable readable(file, buffer);
      readable.reset(100u);
      CHECK_FALSE(readable.lastError().has_value());
      CHECK(readable.bytesLeft() == 0u);
      CHECK_FALSE(readable.available());
      CHECK(readable.read().error() == Readable::EndOfStream);
   }

   TEST_CASE("FileReadable with an empty buffer cannot read") {
      Fixture fixture;
      fixture.stage("/f", 4u);
      File file = fixture.open("/f", OpenMode::Read);
      FileReadable readable(file, std::span<uint8_t>());
      CHECK(readable.read().error() == Readable::ReadError);
   }

   TEST_CASE("FileReadable maps File errors onto Readable::Error and keeps the cause") {
      Fixture fixture;
      fixture.stage("/f", 9000u);

      SUBCASE("a device timeout is Readable::Timeout") {
         File file = fixture.open("/f", OpenMode::Read);
         std::array<uint8_t, 16> buffer{};
         FileReadable readable(file, buffer);
         fixture.failing.failWith = Error::Timeout;
         CHECK(readable.read().error() == Readable::Timeout);
         CHECK(readable.lastError() == Error::Timeout);
         CHECK(readable.position() == 0u); // a failed refill leaves no buffered bytes behind
      }

      SUBCASE("any other error is Readable::ReadError") {
         File file = fixture.open("/f", OpenMode::Read);
         std::array<uint8_t, 16> buffer{};
         FileReadable readable(file, buffer);
         fixture.failing.failWith = Error::Io;
         CHECK(readable.read().error() == Readable::ReadError);
         CHECK(readable.lastError() == Error::Io);
      }

      SUBCASE("a write-only file is ReadError with the storage cause") {
         File file = fixture.open("/f", OpenMode::Append);
         std::array<uint8_t, 16> buffer{};
         FileReadable readable(file, buffer);
         CHECK(readable.read().error() == Readable::ReadError);
         CHECK(readable.lastError() == Error::InvalidArgument);
      }

      SUBCASE("a closed file is ReadError with BadHandle") {
         File file = fixture.open("/f", OpenMode::Read);
         std::array<uint8_t, 16> buffer{};
         FileReadable readable(file, buffer);
         REQUIRE(file.close().has_value());
         CHECK(readable.read().error() == Readable::ReadError);
         CHECK(readable.lastError() == Error::BadHandle);
         CHECK_FALSE(readable.available());
         CHECK(readable.bytesLeft() == 0u);
         CHECK(readable.position() == 0u);
      }
   }

   TEST_CASE("the error mapping covers every storage error") {
      for(uint8_t code = 0u; code <= static_cast<uint8_t>(Error::NotReady); ++code) {
         const auto error = static_cast<Error>(code);
         CAPTURE(toString(error));
         const bool timeout = error == Error::Timeout;
         CHECK(FileReadable::toReadableError(error) == (timeout ? Readable::Timeout : Readable::ReadError));
         const bool noRoom = error == Error::NoSpace || error == Error::TooBig;
         CHECK(FileWritable::toWritableError(error) == (noRoom ? Writable::Overflow : Writable::CouldNotWrite));
      }
   }

   TEST_CASE("FileWritable writes are all-or-error and report the full size") {
      Fixture fixture;
      File file = fixture.open("/out", OpenMode::Write);
      FileWritable writable(file);
      const auto data = Fixture::pattern(5000u);

      const auto first = writable.write(std::span<const uint8_t>(data).first(3000u));
      REQUIRE(first.has_value());
      CHECK(*first == 3000u);
      const auto second = writable.write(std::span<const uint8_t>(data).subspan(3000u));
      REQUIRE(second.has_value());
      CHECK(*second == 2000u);
      CHECK(writable.position() == 5000u);
      CHECK(writable.written() == 5000u);
      REQUIRE(writable.flush().has_value());
      REQUIRE(file.close().has_value());

      std::vector<uint8_t> readBack(data.size());
      CHECK(fixture.fileSystem.readFile("/out", readBack).value() == data.size());
      CHECK(readBack == data);
   }

   TEST_CASE("FileWritable takes the convenience writes of Writable") {
      Fixture fixture;
      File file = fixture.open("/out", OpenMode::Write);
      FileWritable writable(file);
      CHECK(writable.printf("%d-%s", 42, "x").value() == 4u);
      CHECK(writable.writeNumber<uint16_t>(0x1234u).value() == 2u);
      REQUIRE(file.close().has_value());

      std::array<uint8_t, 8> readBack{};
      REQUIRE(fixture.fileSystem.readFile("/out", readBack).value() == 6u);
      CHECK(std::string_view(reinterpret_cast<const char *>(readBack.data()), 4u) == "42-x");
      CHECK(readBack[4] == 0x34u); // system endianness, little endian on the hosts that run these tests
   }

   TEST_CASE("FileWritable reset moves the write position and overwrites in place") {
      Fixture fixture;
      File file = fixture.open("/out", OpenMode::ReadWriteTruncate);
      FileWritable writable(file);
      const std::array<uint8_t, 6> original{ 1, 2, 3, 4, 5, 6 };
      REQUIRE(writable.write(original).has_value());

      writable.reset(2u);
      CHECK(writable.position() == 2u);
      const std::array<uint8_t, 2> patch{ 0xAA, 0xBB };
      REQUIRE(writable.write(patch).has_value());
      REQUIRE(writable.flush().has_value());

      std::array<uint8_t, 6> readBack{};
      REQUIRE(file.seek(0u).has_value());
      REQUIRE(file.read(readBack).value() == 6u);
      CHECK(readBack == std::array<uint8_t, 6>{ 1, 2, 0xAA, 0xBB, 5, 6 });
   }

   TEST_CASE("FileWritable maps File errors onto Writable::Error and keeps the cause") {
      Fixture fixture;
      fixture.stage("/f", 4u);
      const std::array<uint8_t, 4> data{ 1, 2, 3, 4 };

      SUBCASE("a full medium is Writable::Overflow") {
         File file = fixture.open("/big", OpenMode::Write);
         FileWritable writable(file);
         const auto chunk = Fixture::pattern(4096u);
         std::optional<Writable::Error> failure;
         // The device holds 64 KiB, so this must fail well before 32 chunks.
         for(uint32_t i = 0u; i < 32u && !failure; ++i) {
            const auto result = writable.write(chunk);
            if(!result) {
               failure = result.error();
            }
         }
         REQUIRE(failure.has_value());
         CHECK(*failure == Writable::Overflow);
         CHECK(writable.lastError() == Error::NoSpace);
      }

      SUBCASE("a read-only file is CouldNotWrite") {
         File file = fixture.open("/f", OpenMode::Read);
         FileWritable writable(file);
         CHECK(writable.write(data).error() == Writable::CouldNotWrite);
         CHECK(writable.lastError() == Error::InvalidArgument);
      }

      SUBCASE("a closed file is CouldNotWrite with BadHandle, also for flush") {
         File file = fixture.open("/f", OpenMode::Write);
         FileWritable writable(file);
         REQUIRE(file.close().has_value());
         CHECK(writable.write(data).error() == Writable::CouldNotWrite);
         CHECK(writable.lastError() == Error::BadHandle);
         CHECK(writable.flush().error() == Writable::CouldNotWrite);
         CHECK(writable.position() == 0u);
      }
   }

   TEST_CASE("a file written through BufferedWritable and FileWritable reads back through FileReadable") {
      Fixture fixture;
      const auto data = Fixture::pattern(7777u);
      {
         File file = fixture.open("/staged", OpenMode::Write);
         FileWritable sink(file);
         std::array<uint8_t, 256> bufferMemory{};
         BufferedWritable buffered(sink, bufferMemory);
         REQUIRE(buffered.write(std::span<const uint8_t>(data).first(10u)).has_value());
         REQUIRE(buffered.write(std::span<const uint8_t>(data).subspan(10u)).has_value());
         REQUIRE(buffered.flush().has_value());
         REQUIRE(file.close().has_value());
      }

      File file = fixture.open("/staged", OpenMode::Read);
      std::array<uint8_t, 64> buffer{};
      FileReadable readable(file, buffer);
      SHA256Digest digest;
      DigestReadable hashing(readable, digest);
      std::vector<uint8_t> received(data.size());
      REQUIRE(hashing.read(std::span<uint8_t>(received)).has_value());
      CHECK(received == data);
      std::array<uint8_t, 32> hash{};
      REQUIRE(digest.getDigest(hash).has_value());
      CHECK(hash == sha256Of(data));
   }
}
