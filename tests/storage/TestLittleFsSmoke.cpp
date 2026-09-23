#include "RamBlockDevice.h"
#include "storage/BlockDeviceDecorator.h"
#include "storage/littlefs/LittleFsFileSystem.h"
#include <array>
#include <doctest/doctest.h>
#include <string>
#include <vector>

// Minimal smoke test of the LittleFS binding over the real littlefs. The full suite is a separate issue.

using namespace art::storage;
using namespace art::storage::test;

namespace {

// NOR-like: 16 byte read and program unit, 4 KiB erase unit, 16 blocks.
// clang-format off
constexpr Geometry kGeometry{
   .readSize = 16u, .programSize = 16u, .eraseSize = 4096u, .size = 64u * 1024u, .eraseBeforeProgram = true
};
// clang-format on

template <typename T>
bool failedWith(const std::expected<T, Error> &result, Error error) {
   return !result.has_value() && result.error() == error;
}

std::vector<uint8_t> bytes(std::string_view text) {
   return { text.begin(), text.end() };
}

// Fails reads with a chosen error, to check that a transient device error is not flattened into Io.
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
      : device(kGeometry, backing) {}

   std::array<uint8_t, 64u * 1024u> backing{};
   RamBlockDevice device;
   LittleFsFileSystem<> fileSystem{ device };
};

} // namespace

TEST_SUITE("art::storage::LittleFsFileSystem smoke") {

   TEST_CASE("mount formats a blank device and the data survives a remount") {
      Fixture fixture;
      REQUIRE(fixture.fileSystem.mount().has_value());
      CHECK(fixture.fileSystem.isMounted());
      REQUIRE(fixture.fileSystem.writeFile("/a", bytes("hello")).has_value());
      REQUIRE(fixture.fileSystem.unmount().has_value());
      CHECK_FALSE(fixture.fileSystem.isMounted());

      REQUIRE(fixture.fileSystem.mount().has_value());
      std::array<uint8_t, 16> buffer{};
      CHECK(fixture.fileSystem.readFile("/a", buffer).value() == 5u);
   }

   TEST_CASE("mount replaces garbage that is not a littlefs volume with a fresh one") {
      Fixture fixture;
      std::ranges::fill(fixture.backing, 0x5Au);
      REQUIRE(fixture.fileSystem.mount().has_value());
      CHECK(fixture.fileSystem.writeFile("/a", bytes("x")).has_value());
   }

   TEST_CASE("a read error during mount is reported and does not format the volume") {
      std::array<uint8_t, 64u * 1024u> backing{};
      RamBlockDevice ram{ kGeometry, backing };
      {
         LittleFsFileSystem<> fileSystem{ ram };
         REQUIRE(fileSystem.mount().has_value());
         REQUIRE(fileSystem.writeFile("/keep", bytes("data")).has_value());
      }
      FailingReadDevice failing{ ram };
      LittleFsFileSystem<> fileSystem{ failing };
      failing.failWith = Error::Timeout;
      CHECK(failedWith(fileSystem.mount(), Error::Timeout)); // the device error, not a flat Io
      CHECK_FALSE(fileSystem.isMounted());

      failing.failWith.reset();
      REQUIRE(fileSystem.mount().has_value());
      std::array<uint8_t, 8> buffer{};
      CHECK(fileSystem.readFile("/keep", buffer).value() == 4u);
   }

   TEST_CASE("whole-file round trip, stat, directory listing and free space") {
      Fixture fixture;
      auto &fileSystem = fixture.fileSystem;
      REQUIRE(fileSystem.mount().has_value());
      const offset_t freeBefore = fileSystem.freeSpace().value();
      REQUIRE(fileSystem.mkdir("/dir").has_value());
      REQUIRE(fileSystem.writeFile("/dir/one", bytes("1")).has_value());
      REQUIRE(fileSystem.writeFile("/dir/two", bytes("22")).has_value());

      CHECK(fileSystem.stat("/dir/two").value().size == 2u);
      CHECK(fileSystem.stat("/dir").value().type == EntryType::Directory);
      CHECK(failedWith(fileSystem.stat("/nope"), Error::NoEntry));
      CHECK(failedWith(fileSystem.mkdir("/dir"), Error::Exists));
      CHECK(fileSystem.freeSpace().value() < freeBefore);

      struct Names {
         std::vector<std::string> names;
         bool visit(const char *name, const FileInfo &) {
            names.emplace_back(name);
            return true;
         }
      } collected;
      const auto visited = fileSystem.visitDirectory("/dir", DirectoryVisitor::create<Names, &Names::visit>(collected));
      CHECK(visited.value() == 2u); // "." and ".." are not reported
      CHECK(collected.names.size() == 2u);
      CHECK(failedWith(fileSystem.remove("/dir"), Error::NotEmpty));
   }

   TEST_CASE("geometry that littlefs cannot use is rejected instead of mounted") {
      std::array<uint8_t, 64u * 1024u> backing{};
      SUBCASE("no erase unit") {
         RamBlockDevice ram{ Geometry{ .readSize = 1u, .programSize = 1u, .size = 64u * 1024u }, backing };
         LittleFsFileSystem<> fileSystem{ ram };
         CHECK(failedWith(fileSystem.mount(), Error::InvalidArgument));
         CHECK_FALSE(fileSystem.isMounted());
      }
      SUBCASE("cache is not a multiple of the read size") {
         Geometry geometry = kGeometry;
         geometry.readSize = 512u;
         geometry.programSize = 512u;
         RamBlockDevice ram{ geometry, backing };
         LittleFsFileSystem<> fileSystem{ ram };
         CHECK(failedWith(fileSystem.mount(), Error::InvalidArgument));
      }
      SUBCASE("cache does not divide the erase unit") {
         RamBlockDevice ram{ kGeometry, backing };
         LittleFsFileSystem<2, 96> fileSystem{ ram };
         CHECK(failedWith(fileSystem.mount(), Error::InvalidArgument));
      }
      SUBCASE("format is rejected as well") {
         RamBlockDevice ram{ Geometry{ .readSize = 1u, .programSize = 1u, .size = 64u * 1024u }, backing };
         LittleFsFileSystem<> fileSystem{ ram };
         CHECK(failedWith(fileSystem.format(), Error::InvalidArgument));
      }
   }

   TEST_CASE("a device with a coarser read size than program size works") {
      std::array<uint8_t, 64u * 1024u> backing{};
      Geometry geometry = kGeometry;
      geometry.readSize = 64u;
      geometry.programSize = 64u;
      RamBlockDevice ram{ geometry, backing };
      LittleFsFileSystem<> fileSystem{ ram };
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/a", bytes("coarse")).has_value());
      std::array<uint8_t, 8> buffer{};
      CHECK(fileSystem.readFile("/a", buffer).value() == 6u);
   }

   TEST_CASE("format needs an unmounted file system") {
      Fixture fixture;
      REQUIRE(fixture.fileSystem.mount().has_value());
      CHECK(failedWith(fixture.fileSystem.format(), Error::Busy));
      REQUIRE(fixture.fileSystem.unmount().has_value());
      CHECK(fixture.fileSystem.format().has_value());
      CHECK(failedWith(fixture.fileSystem.stat("/"), Error::NotMounted));
   }

   TEST_CASE("handles: stale after unmount, reused slot stays distinct, pool exhaustion") {
      Fixture fixture;
      auto &fileSystem = fixture.fileSystem;
      REQUIRE(fileSystem.mount().has_value());

      auto stale = fileSystem.open("/a", OpenMode::Write);
      REQUIRE(stale.has_value());
      REQUIRE(fileSystem.unmount().has_value());
      REQUIRE(fileSystem.mount().has_value());
      auto fresh = fileSystem.open("/b", OpenMode::Write);
      REQUIRE(fresh.has_value());
      std::array<uint8_t, 4> buffer{};
      CHECK(failedWith(stale->read(buffer), Error::BadHandle));
      CHECK_FALSE(stale->isOpen());
      CHECK(fresh->isOpen());

      auto second = fileSystem.open("/c", OpenMode::Write);
      REQUIRE(second.has_value());
      CHECK(failedWith(fileSystem.open("/d", OpenMode::Write), Error::NoMemory)); // pool of 2
      REQUIRE(second->close().has_value());
      CHECK(failedWith(second->close(), Error::BadHandle));
      CHECK(fileSystem.open("/d", OpenMode::Write).has_value());
   }

   TEST_CASE("seek and write into the middle of a file, then read it back") {
      Fixture fixture;
      auto &fileSystem = fixture.fileSystem;
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/image", bytes("aaaaaaaa")).has_value());
      {
         auto file = fileSystem.open("/image", OpenMode::ReadWrite);
         REQUIRE(file.has_value());
         REQUIRE(file->seek(3u).has_value());
         REQUIRE(file->write(bytes("XY")).has_value());
         CHECK(file->tell().value() == 5u);
         CHECK(file->size().value() == 8u);
         REQUIRE(file->close().has_value());
      }
      std::array<uint8_t, 16> buffer{};
      const auto count = fileSystem.readFile("/image", buffer);
      REQUIRE(count.has_value());
      CHECK(std::string(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*count)) == "aaaXYaaa");
   }

   TEST_CASE("open modes map to littlefs flags") {
      Fixture fixture;
      auto &fileSystem = fixture.fileSystem;
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/a", bytes("abc")).has_value());

      CHECK(failedWith(fileSystem.open("/missing", OpenMode::Read), Error::NoEntry));
      CHECK(failedWith(fileSystem.open("/a", OpenMode::CreateNew), Error::Exists));
      CHECK(failedWith(fileSystem.open("/", OpenMode::Read), Error::IsDirectory));
      {
         auto file = fileSystem.open("/a", OpenMode::Append);
         REQUIRE(file.has_value());
         CHECK(file->tell().value() == 3u);
         REQUIRE(file->write(bytes("de")).has_value());
      }
      {
         auto readOnly = fileSystem.open("/a", OpenMode::Read);
         REQUIRE(readOnly.has_value());
         CHECK(failedWith(readOnly->write(bytes("x")), Error::InvalidArgument));
      }
      {
         auto truncated = fileSystem.open("/a", OpenMode::ReadWriteTruncate);
         REQUIRE(truncated.has_value());
         CHECK(truncated->size().value() == 0u);
      }
   }

   TEST_CASE("remove and rename of an open file are refused, and rename replaces a closed target") {
      Fixture fixture;
      auto &fileSystem = fixture.fileSystem;
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/a", bytes("old")).has_value());
      REQUIRE(fileSystem.writeFile("/b", bytes("new")).has_value());
      {
         auto file = fileSystem.open("/a", OpenMode::ReadWrite);
         REQUIRE(file.has_value());
         CHECK(failedWith(fileSystem.remove("/a"), Error::Busy));
         CHECK(failedWith(fileSystem.remove("//./a"), Error::Busy)); // another spelling of the same path
         CHECK(failedWith(fileSystem.rename("/a", "/c"), Error::Busy));
         CHECK(failedWith(fileSystem.rename("/b", "/a"), Error::Busy));
         CHECK(fileSystem.rename("/a", "/a").has_value());
         CHECK(file->write(bytes("!")).has_value());
      }
      REQUIRE(fileSystem.rename("/b", "/a").has_value());
      std::array<uint8_t, 8> buffer{};
      CHECK(fileSystem.readFile("/a", buffer).value() == 3u);
      CHECK(failedWith(fileSystem.stat("/b"), Error::NoEntry));
   }

   TEST_CASE("two handles on one file are only allowed when both read") {
      Fixture fixture;
      auto &fileSystem = fixture.fileSystem;
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/a", bytes("abc")).has_value());
      auto readerOne = fileSystem.open("/a", OpenMode::Read);
      auto readerTwo = fileSystem.open("/a", OpenMode::Read);
      CHECK(readerOne.has_value());
      CHECK(readerTwo.has_value());
      CHECK(failedWith(fileSystem.open("/a", OpenMode::ReadWrite), Error::Busy));
      CHECK(failedWith(fileSystem.open("/a", OpenMode::CreateNew), Error::Exists));
   }

   TEST_CASE("paths the interface does not define are rejected") {
      Fixture fixture;
      auto &fileSystem = fixture.fileSystem;
      REQUIRE(fileSystem.mount().has_value());
      CHECK(failedWith(fileSystem.open("relative", OpenMode::Write), Error::InvalidArgument));
      CHECK(failedWith(fileSystem.open("/a/../b", OpenMode::Write), Error::InvalidArgument));
      CHECK(failedWith(fileSystem.open(nullptr, OpenMode::Write), Error::InvalidArgument));
      const std::string tooLong = "/" + std::string(kMaxNameLength + 1u, 'n');
      CHECK(failedWith(fileSystem.open(tooLong.c_str(), OpenMode::Read), Error::NameTooLong));
      const std::string longest = "/" + std::string(kMaxNameLength, 'n');
      CHECK(fileSystem.writeFile(longest.c_str(), bytes("x")).has_value());
   }

   TEST_CASE("destroying a mounted file system unmounts it and keeps the data") {
      std::array<uint8_t, 64u * 1024u> backing{};
      RamBlockDevice ram{ kGeometry, backing };
      {
         LittleFsFileSystem<> fileSystem{ ram };
         REQUIRE(fileSystem.mount().has_value());
         REQUIRE(fileSystem.writeFile("/a", bytes("stored")).has_value());
      }
      LittleFsFileSystem<> fileSystem{ ram };
      REQUIRE(fileSystem.mount().has_value());
      std::array<uint8_t, 8> buffer{};
      CHECK(fileSystem.readFile("/a", buffer).value() == 6u);
   }
}
