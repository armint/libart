#include "FlakyReadDevice.h"
#include "RamBlockDevice.h"
#include "storage/littlefs/LittleFsFileSystem.h"
#include <array>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <vector>

// littlefs marks a range of its read cache as loaded before the device read that fills it, and keeps that mark when
// the read fails. The binding must drop the caches itself; without it a later read returns stale bytes as success.
// These tests fail a device read, let the device recover, and require the very next operations to be correct.

using namespace art::storage;
using namespace art::storage::test;

namespace {

// NOR-like: 16 byte read and program unit, 4 KiB erase unit, 16 blocks.
// clang-format off
constexpr Geometry kGeometry{
   .readSize = 16u, .programSize = 16u, .eraseSize = 4096u, .size = 64u * 1024u, .eraseBeforeProgram = true
};
// clang-format on

struct Fixture {
   Fixture()
      : ram(kGeometry, backing)
      , flaky(ram)
      , fileSystem(flaky) {
      REQUIRE(fileSystem.mount().has_value());
   }

   // A non-repeating pattern, so a stale or misplaced range differs from the real content.
   static std::vector<uint8_t> pattern(size_t size) {
      std::vector<uint8_t> data(size);
      for(size_t i = 0; i < size; ++i) {
         data[i] = static_cast<uint8_t>(((i * 7u) ^ (i >> 8u)) & 0x7Fu); // never 0xFF, the fill of a fresh cache
      }
      return data;
   }

   // An unmount and mount empties every cache, so each sweep step starts from the same cold state.
   void remount() {
      REQUIRE(fileSystem.unmount().has_value());
      REQUIRE(fileSystem.mount().has_value());
   }

   std::vector<uint8_t> stage(const char *path, size_t size) {
      auto data = pattern(size);
      REQUIRE(fileSystem.writeFile(path, data).has_value());
      return data;
   }

   std::array<uint8_t, 64u * 1024u> backing{};
   RamBlockDevice ram;
   FlakyReadDevice flaky;
   LittleFsFileSystem<> fileSystem;
};

// Reads the file from the current position to its end, in small chunks so that every block of it is touched.
art::expected<std::vector<uint8_t>, Error> readToEnd(File &file) {
   std::vector<uint8_t> content;
   std::array<uint8_t, 100> chunk{};
   while(true) {
      const auto count = file.read(chunk);
      if(!count) {
         return art::unexpected(count.error());
      }
      if(*count == 0u) {
         return content;
      }
      content.insert(content.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(*count));
   }
}

} // namespace

TEST_SUITE("LittleFsFileSystem read cache after a failed device read") {

   TEST_CASE("a fresh handle reads the real content after its first read failed") {
      Fixture fixture;
      const auto data = fixture.stage("/f", 9000u);
      fixture.remount();
      auto file = fixture.fileSystem.open("/f", OpenMode::Read);
      REQUIRE(file.has_value());

      fixture.flaky.armAfter(0u);
      std::array<uint8_t, 32> buffer{};
      const auto failed = file->read(buffer);
      REQUIRE_FALSE(failed.has_value());
      CHECK((failed.error() == Error::Timeout));

      fixture.flaky.disarm();
      const auto content = readToEnd(*file);
      REQUIRE(content.has_value());
      CHECK(*content == data);
   }

   TEST_CASE("a handle reads the real content after a read in the middle of the file failed") {
      Fixture fixture;
      const auto data = fixture.stage("/f", 9000u);
      fixture.remount();
      auto file = fixture.fileSystem.open("/f", OpenMode::Read);
      REQUIRE(file.has_value());

      // Fill the caches with the start of the file, so that the failing read is for a different range. A read
      // that fails for the range the cache already holds would still return the right bytes from stale data.
      std::array<uint8_t, 7> chunk{};
      REQUIRE(file->read(chunk).value() == chunk.size());
      constexpr offset_t kFarPosition = 6000u;
      REQUIRE(file->seek(kFarPosition).has_value());
      fixture.flaky.armAfter(0u);
      const auto failed = file->read(chunk);
      REQUIRE_FALSE(failed.has_value());
      fixture.flaky.disarm();

      const auto position = file->tell();
      REQUIRE(position.has_value());
      const auto rest = readToEnd(*file);
      REQUIRE(rest.has_value());
      CHECK(*rest == std::vector<uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(*position), data.end()));

      // A second handle sees the whole file too.
      auto again = fixture.fileSystem.open("/f", OpenMode::Read);
      REQUIRE(again.has_value());
      CHECK(readToEnd(*again).value() == data);
   }

   TEST_CASE("a failed read at any point of a file leaves it readable") {
      // 20 bytes are inline in the metadata of the directory; 3000 bytes have blocks of their own.
      for(const size_t size : { size_t{ 20u }, size_t{ 3000u } }) {
         CAPTURE(size);
         Fixture fixture;
         const auto data = fixture.stage("/f", size);
         bool reachedEnd = false;
         for(uint32_t failAfter = 0u; failAfter < 2000u && !reachedEnd; ++failAfter) {
            CAPTURE(failAfter);
            fixture.remount();
            auto file = fixture.fileSystem.open("/f", OpenMode::Read);
            REQUIRE(file.has_value());
            fixture.flaky.armAfter(failAfter);
            const auto attempt = readToEnd(*file);
            fixture.flaky.disarm();
            reachedEnd = attempt.has_value(); // this step let the whole read pass, so later ones would too

            REQUIRE(file->seek(0u).has_value());
            const auto content = readToEnd(*file);
            REQUIRE(content.has_value());
            CHECK(*content == data);
         }
         CHECK(reachedEnd);
      }
   }

   TEST_CASE("a failed directory traversal at any point leaves the directory listable") {
      Fixture fixture;
      REQUIRE(fixture.fileSystem.mkdir("/dir").has_value());
      for(uint32_t i = 0u; i < 12u; ++i) {
         const std::string path = "/dir/file" + std::to_string(i);
         REQUIRE(fixture.fileSystem.writeFile(path.c_str(), fixture.pattern(i + 1u)).has_value());
      }

      struct Counter {
         size_t count{ 0u };
         bool visit(const char *, const FileInfo &) {
            ++count;
            return true;
         }
      };

      bool reachedEnd = false;
      for(uint32_t failAfter = 0u; failAfter < 2000u && !reachedEnd; ++failAfter) {
         CAPTURE(failAfter);
         fixture.remount();
         Counter failing;
         fixture.flaky.armAfter(failAfter);
         const auto attempt =
         fixture.fileSystem.visitDirectory("/dir", DirectoryVisitor::create<Counter, &Counter::visit>(failing));
         fixture.flaky.disarm();
         reachedEnd = attempt.has_value();

         Counter counter;
         const auto visited =
         fixture.fileSystem.visitDirectory("/dir", DirectoryVisitor::create<Counter, &Counter::visit>(counter));
         REQUIRE(visited.has_value());
         CHECK(*visited == 12u);
         CHECK(counter.count == 12u);
      }
      CHECK(reachedEnd);
   }

   TEST_CASE("a failed read of one file does not lose the unsynced data of another that is being written") {
      Fixture fixture;
      const auto other = fixture.stage("/other", 9000u);
      fixture.remount();
      auto writer = fixture.fileSystem.open("/out", OpenMode::Write);
      auto reader = fixture.fileSystem.open("/other", OpenMode::Read);
      REQUIRE(writer.has_value());
      REQUIRE(reader.has_value());

      const auto pending = Fixture::pattern(1000u); // its tail is still in the write cache, not on the device
      REQUIRE(writer->write(pending).has_value());
      fixture.flaky.armAfter(0u);
      std::array<uint8_t, 32> buffer{};
      REQUIRE_FALSE(reader->read(buffer).has_value());
      fixture.flaky.disarm();

      REQUIRE(writer->close().has_value());
      std::array<uint8_t, 1200> stored{};
      const auto count = fixture.fileSystem.readFile("/out", stored);
      REQUIRE(count.has_value());
      CHECK(std::vector<uint8_t>(stored.begin(), stored.begin() + static_cast<std::ptrdiff_t>(*count)) == pending);
      CHECK(readToEnd(*reader).value() == other);
   }

   TEST_CASE("stat and free space are right after a failed read") {
      Fixture fixture;
      fixture.stage("/f", 5000u);
      const offset_t freeBefore = fixture.fileSystem.freeSpace().value();
      bool reachedEnd = false;
      for(uint32_t failAfter = 0u; failAfter < 2000u && !reachedEnd; ++failAfter) {
         CAPTURE(failAfter);
         fixture.remount();
         fixture.flaky.armAfter(failAfter);
         reachedEnd = fixture.fileSystem.stat("/f").has_value() && fixture.fileSystem.freeSpace().has_value();
         fixture.flaky.disarm();

         const auto info = fixture.fileSystem.stat("/f");
         REQUIRE(info.has_value());
         CHECK(info->size == 5000u);
         CHECK(fixture.fileSystem.freeSpace().value() == freeBefore);
      }
      CHECK(reachedEnd);
   }
}
