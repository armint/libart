#include "MockCipherBlockDevice.h"
#include "RamBlockDevice.h"
#include "storage/BlockDeviceDecorator.h"
#include "storage/littlefs/LittleFsFileSystem.h"
#include <algorithm>
#include <array>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Host tests of the LittleFS binding on the real littlefs.
//
// Every test case of the "LittleFsFileSystem" suite is a template over a device stack and runs twice with
// identical expectations:
//   DirectStack: LittleFsFileSystem -> PowerCutDevice -> RamBlockDevice
//   CipherStack: LittleFsFileSystem -> MockCipherBlockDevice -> PowerCutDevice -> RamBlockDevice
// The second run proves that a transform device below the file system changes nothing: after the transform an
// erased range no longer reads back as 0xFF, and littlefs must still tell it from valid metadata by its own
// checksums. The PowerCutDevice sits directly on the RAM medium, so a simulated power cut hits the ciphertext
// like real hardware would. Cases that are specific to one stack are outside the templates and say so.

using namespace art::storage;
using namespace art::storage::test;

namespace {

using FileSystemUnderTest = LittleFsFileSystem<>;

// NOR-like: byte reads, 16 byte program unit, 4 KiB erase unit, 32 blocks. The cipher raises readSize to its
// 16 byte sector size, so the two stacks also cover different geometries reported to littlefs.
// clang-format off
constexpr Geometry kGeometry{
   .readSize = 1u, .programSize = 16u, .eraseSize = 4096u, .size = 128u * 1024u, .eraseBeforeProgram = true
};
// clang-format on

template <typename T>
bool failedWith(const std::expected<T, Error> &result, Error error) {
   return !result.has_value() && result.error() == error;
}

std::vector<uint8_t> bytes(std::string_view text) {
   return { text.begin(), text.end() };
}

// Deterministic, position dependent content that makes a misplaced byte visible.
std::vector<uint8_t> pattern(size_t size, uint8_t seed = 1u) {
   std::vector<uint8_t> data(size);
   for(size_t i = 0; i < size; ++i) {
      data[i] = static_cast<uint8_t>((i * 31u + seed) ^ (i >> 8u));
   }
   return data;
}

std::vector<uint8_t> readAll(FileSystem &fileSystem, const char *path, size_t capacity) {
   std::vector<uint8_t> buffer(capacity);
   const auto count = fileSystem.readFile(path, buffer);
   REQUIRE(count.has_value());
   buffer.resize(*count);
   return buffer;
}

// Fails the next failCount reads with a chosen error, then behaves like the device below. That lets a test
// fail the mount's first read and still let a (wrongly) following format succeed.
class FailingReadDevice final : public BlockDeviceDecorator {
   public:
   using BlockDeviceDecorator::BlockDeviceDecorator;

   std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) override {
      if(failWith && failCount > 0u) {
         --failCount;
         return std::unexpected(*failWith);
      }
      return BlockDeviceDecorator::read(offset, buffer);
   }

   std::optional<Error> failWith;
   uint32_t failCount{ UINT32_MAX };
};

/**
 * Simulates a power cut on the medium. It counts program and erase operations. When the armed operation is
 * reached, that operation is torn (only its first tearBytes bytes take effect) and fails with Error::Io, and
 * from then on every program, erase and sync fails, as if the power were gone. Reads still work. restorePower()
 * ends the outage.
 */
class PowerCutDevice final : public BlockDeviceDecorator {
   public:
   explicit PowerCutDevice(RamBlockDevice &ram)
      : BlockDeviceDecorator(ram)
      , ram_(ram) {}

   /// The `writeOperation`-th following program or erase (1 is the next one) is torn after `tearBytes`.
   void armCut(uint32_t writeOperation, size_t tearBytes) {
      writeOperationsUntilCut_ = writeOperation;
      tearBytes_ = tearBytes;
   }

   void restorePower() {
      writeOperationsUntilCut_ = 0u;
      cut_ = false;
   }

   [[nodiscard]] bool hasCut() const {
      return cut_;
   }

   /// Program and erase operations since the last resetCount().
   [[nodiscard]] uint32_t writeOperations() const {
      return writeOperations_;
   }

   void resetCount() {
      writeOperations_ = 0u;
   }

   std::expected<void, Error> program(offset_t offset, std::span<const uint8_t> data) override {
      if(cut_) {
         return std::unexpected(Error::Io);
      }
      ++writeOperations_;
      if(writeOperationsUntilCut_ != 0u && --writeOperationsUntilCut_ == 0u) {
         cut_ = true;
         ram_.truncateNextProgram(tearBytes_);
         static_cast<void>(BlockDeviceDecorator::program(offset, data));
         ram_.clearFaults();
         return std::unexpected(Error::Io);
      }
      return BlockDeviceDecorator::program(offset, data);
   }

   std::expected<void, Error> erase(offset_t offset, offset_t length) override {
      if(cut_) {
         return std::unexpected(Error::Io);
      }
      ++writeOperations_;
      if(writeOperationsUntilCut_ != 0u && --writeOperationsUntilCut_ == 0u) {
         cut_ = true;
         const size_t erased = std::min<size_t>(tearBytes_, length);
         std::ranges::fill(ram_.contents().subspan(static_cast<size_t>(offset), erased), RamBlockDevice::kErasedByte);
         return std::unexpected(Error::Io);
      }
      return BlockDeviceDecorator::erase(offset, length);
   }

   std::expected<void, Error> sync() override {
      if(cut_) {
         return std::unexpected(Error::Io);
      }
      return BlockDeviceDecorator::sync();
   }

   private:
   RamBlockDevice &ram_;
   bool cut_{ false };
   uint32_t writeOperationsUntilCut_{ 0u };
   size_t tearBytes_{ 0u };
   uint32_t writeOperations_{ 0u };
};

// The two device stacks. The members are declared in the order they are built.
struct DirectStack {
   static constexpr bool kTransformed = false;

   explicit DirectStack(const Geometry &geometry = kGeometry)
      : backing(geometry.size)
      , ram(geometry, backing)
      , powerCut(ram) {}

   BlockDevice &device() {
      return powerCut;
   }

   std::vector<uint8_t> backing;
   RamBlockDevice ram;
   PowerCutDevice powerCut;
};

struct CipherStack {
   static constexpr bool kTransformed = true;

   explicit CipherStack(const Geometry &geometry = kGeometry)
      : backing(geometry.size)
      , ram(geometry, backing)
      , powerCut(ram)
      , cipher(powerCut) {}

   BlockDevice &device() {
      return cipher;
   }

   std::vector<uint8_t> backing;
   RamBlockDevice ram;
   PowerCutDevice powerCut;
   MockCipherBlockDevice cipher;
};

} // namespace

// A test case that runs once per device stack, with the stack type named Stack.
#define STACKED_TEST_CASE(name) TEST_CASE_TEMPLATE(name, Stack, DirectStack, CipherStack)

TEST_SUITE("LittleFsFileSystem") {

   STACKED_TEST_CASE("mount formats a blank device and the data survives a remount") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      CHECK(fileSystem.isMounted());
      CHECK(stack.ram.eraseCount() > 0u); // a blank medium was formatted
      REQUIRE(fileSystem.writeFile("/a", bytes("hello")).has_value());
      REQUIRE(fileSystem.unmount().has_value());
      CHECK_FALSE(fileSystem.isMounted());

      const uint32_t erasesBefore = stack.ram.eraseCount();
      REQUIRE(fileSystem.mount().has_value());
      CHECK(stack.ram.eraseCount() == erasesBefore); // an existing volume is not formatted again
      CHECK(readAll(fileSystem, "/a", 16) == bytes("hello"));
   }

   STACKED_TEST_CASE("mount replaces garbage that is not a littlefs volume with a fresh one") {
      Stack stack;
      std::ranges::fill(stack.backing, 0x5Au);
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      CHECK(fileSystem.writeFile("/a", bytes("x")).has_value());
   }

   STACKED_TEST_CASE("mount formats only a corrupt volume, never after another error") {
      Stack stack;
      {
         FileSystemUnderTest fileSystem{ stack.device() };
         REQUIRE(fileSystem.mount().has_value());
         REQUIRE(fileSystem.writeFile("/keep", bytes("data")).has_value());
      }
      FailingReadDevice failing{ stack.device() };
      FileSystemUnderTest fileSystem{ failing };
      failing.failWith = Error::Timeout;
      failing.failCount = 1u; // only the first read fails, so a format after it would succeed
      const uint32_t erasesBefore = stack.ram.eraseCount();
      CHECK(failedWith(fileSystem.mount(), Error::Timeout)); // the device error, not a flat Io
      CHECK_FALSE(fileSystem.isMounted());
      CHECK(stack.ram.eraseCount() == erasesBefore); // nothing was formatted

      failing.failWith.reset();
      REQUIRE(fileSystem.mount().has_value());
      CHECK(readAll(fileSystem, "/keep", 8) == bytes("data"));
   }

   STACKED_TEST_CASE("format needs an unmounted file system and empties the volume") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/a", bytes("x")).has_value());
      CHECK(failedWith(fileSystem.format(), Error::Busy));
      REQUIRE(fileSystem.unmount().has_value());
      REQUIRE(fileSystem.format().has_value());
      CHECK_FALSE(fileSystem.isMounted());
      CHECK(failedWith(fileSystem.stat("/"), Error::NotMounted));
      REQUIRE(fileSystem.mount().has_value());
      CHECK(failedWith(fileSystem.stat("/a"), Error::NoEntry));
   }

   STACKED_TEST_CASE("geometry that littlefs cannot use is rejected instead of mounted") {
      SUBCASE("no erase unit") {
         Stack stack{ Geometry{ .readSize = 1u, .programSize = 1u, .size = 64u * 1024u } };
         FileSystemUnderTest fileSystem{ stack.device() };
         CHECK(failedWith(fileSystem.mount(), Error::InvalidArgument));
         CHECK_FALSE(fileSystem.isMounted());
         CHECK(failedWith(fileSystem.format(), Error::InvalidArgument));
      }
      SUBCASE("cache is not a multiple of the read size") {
         Stack stack;
         LittleFsFileSystem<2, 8> fileSystem{ stack.device() }; // the cipher reports readSize 16
         CHECK(failedWith(fileSystem.mount(), Error::InvalidArgument));
      }
      SUBCASE("cache does not divide the erase unit") {
         Stack stack;
         LittleFsFileSystem<2, 96> fileSystem{ stack.device() };
         CHECK(failedWith(fileSystem.mount(), Error::InvalidArgument));
      }
      SUBCASE("erase unit below the 128 bytes littlefs needs") {
         // clang-format off
         constexpr Geometry tinyErase{
            .readSize = 1u, .programSize = 16u, .eraseSize = 64u, .size = 64u * 1024u, .eraseBeforeProgram = true
         };
         // clang-format on
         Stack stack{ tinyErase };
         FileSystemUnderTest fileSystem{ stack.device() };
         CHECK(failedWith(fileSystem.mount(), Error::InvalidArgument));
      }
   }

   // Direct stack only: a read size larger than the cipher's sector size is rejected by the cipher itself.
   TEST_CASE("a device with a coarse read and program unit works") {
      DirectStack stack{ Geometry{
      .readSize = 64u, .programSize = 64u, .eraseSize = 4096u, .size = 64u * 1024u, .eraseBeforeProgram = true } };
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/a", bytes("coarse")).has_value());
      CHECK(readAll(fileSystem, "/a", 8) == bytes("coarse"));
   }

   STACKED_TEST_CASE("destroying a mounted file system unmounts it and keeps the data") {
      Stack stack;
      {
         FileSystemUnderTest fileSystem{ stack.device() };
         REQUIRE(fileSystem.mount().has_value());
         REQUIRE(fileSystem.writeFile("/a", bytes("stored")).has_value());
      }
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      CHECK(readAll(fileSystem, "/a", 8) == bytes("stored"));
   }

   STACKED_TEST_CASE("whole-file round trip across cache and block boundaries") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      // Around the 256 byte cache, the 4096 byte block and beyond, including the empty file.
      for(const size_t size : { 0u, 1u, 15u, 16u, 255u, 256u, 257u, 4095u, 4096u, 4097u, 10000u }) {
         const auto content = pattern(size, static_cast<uint8_t>(size));
         REQUIRE(fileSystem.writeFile("/data", content).has_value());
         CHECK(fileSystem.stat("/data").value().size == size);
         CHECK(readAll(fileSystem, "/data", 16384) == content);
      }
   }

   STACKED_TEST_CASE("writeFile replaces a longer file and remove deletes it") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/a", pattern(3000u)).has_value());
      REQUIRE(fileSystem.writeFile("/a", bytes("short")).has_value());
      CHECK(readAll(fileSystem, "/a", 4096) == bytes("short"));
      REQUIRE(fileSystem.remove("/a").has_value());
      CHECK(failedWith(fileSystem.stat("/a"), Error::NoEntry));
      CHECK(failedWith(fileSystem.remove("/a"), Error::NoEntry));
   }

   STACKED_TEST_CASE("readFile refuses a destination smaller than the file instead of returning a short read") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/a", pattern(1000u)).has_value());

      std::vector<uint8_t> tooSmall(999u, 0xAAu);
      CHECK(failedWith(fileSystem.readFile("/a", tooSmall), Error::NoMemory));
      CHECK(std::ranges::all_of(tooSmall, [](uint8_t value) { return value == 0xAAu; })); // nothing was copied
      std::vector<uint8_t> exact(1000u);
      CHECK(fileSystem.readFile("/a", exact).value() == 1000u);
      CHECK(exact == pattern(1000u));
      // The failed read did not leave a file open: the pool of two is still free.
      auto first = fileSystem.open("/a", OpenMode::Read);
      auto second = fileSystem.open("/a", OpenMode::Read);
      CHECK(first.has_value());
      CHECK(second.has_value());
   }

   STACKED_TEST_CASE("stat, mkdir, remove and freeSpace") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      const offset_t freeEmpty = fileSystem.freeSpace().value();
      REQUIRE(fileSystem.mkdir("/dir").has_value());
      REQUIRE(fileSystem.writeFile("/dir/file", pattern(9000u)).has_value());

      CHECK(fileSystem.stat("/dir").value().type == EntryType::Directory);
      CHECK(fileSystem.stat("/dir").value().size == 0u);
      CHECK(fileSystem.stat("/dir/file").value().type == EntryType::File);
      CHECK(fileSystem.stat("/dir/file").value().size == 9000u);
      CHECK(fileSystem.stat("/").value().type == EntryType::Directory);
      CHECK(failedWith(fileSystem.stat("/nope"), Error::NoEntry));
      CHECK(failedWith(fileSystem.mkdir("/dir"), Error::Exists));
      CHECK(failedWith(fileSystem.mkdir("/missing/child"), Error::NoEntry));
      CHECK(failedWith(fileSystem.mkdir("/dir/file/child"), Error::NotDirectory));
      CHECK(failedWith(fileSystem.remove("/dir"), Error::NotEmpty));
      CHECK(failedWith(fileSystem.open("/dir", OpenMode::Read), Error::IsDirectory));
      CHECK(failedWith(fileSystem.open("/dir/file/x", OpenMode::Read), Error::NotDirectory));

      const offset_t freeFull = fileSystem.freeSpace().value();
      CHECK(freeFull < freeEmpty);
      REQUIRE(fileSystem.remove("/dir/file").has_value());
      REQUIRE(fileSystem.remove("/dir").has_value());
      CHECK(fileSystem.freeSpace().value() > freeFull);
   }

   STACKED_TEST_CASE("writing until the volume is full reports NoSpace and leaves the volume usable") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.writeFile("/small", bytes("kept")).has_value());
      const auto huge = pattern(200u * 1024u); // more than the 128 KiB volume
      CHECK(failedWith(fileSystem.writeFile("/huge", huge), Error::NoSpace));
      CHECK(readAll(fileSystem, "/small", 8) == bytes("kept"));
      static_cast<void>(fileSystem.remove("/huge"));
      CHECK(fileSystem.writeFile("/after", pattern(2000u)).has_value());
   }

   STACKED_TEST_CASE("directory visiting skips . and .., reports types and sizes, and stops early") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.mkdir("/d").has_value());
      REQUIRE(fileSystem.writeFile("/d/one", bytes("1")).has_value());
      REQUIRE(fileSystem.writeFile("/d/two", bytes("22")).has_value());
      REQUIRE(fileSystem.mkdir("/d/sub").has_value());
      REQUIRE(fileSystem.writeFile("/other", bytes("x")).has_value());

      struct Collector {
         std::vector<std::pair<std::string, FileInfo>> entries;
         size_t stopAfter{ SIZE_MAX };

         bool visit(const char *name, const FileInfo &info) {
            entries.emplace_back(name, info);
            return entries.size() < stopAfter;
         }
      };
      const auto visit = [&](const char *path, Collector &collector) {
         return fileSystem.visitDirectory(path, DirectoryVisitor::create<Collector, &Collector::visit>(collector));
      };

      Collector all;
      CHECK(visit("/d", all).value() == 3u);
      REQUIRE(all.entries.size() == 3u);
      std::ranges::sort(all.entries, {}, &std::pair<std::string, FileInfo>::first);
      CHECK(all.entries[0].first == "one");
      CHECK(all.entries[0].second.type == EntryType::File);
      CHECK(all.entries[0].second.size == 1u);
      CHECK(all.entries[1].first == "sub");
      CHECK(all.entries[1].second.type == EntryType::Directory);
      CHECK(all.entries[1].second.size == 0u);
      CHECK(all.entries[2].first == "two");
      CHECK(all.entries[2].second.size == 2u);

      Collector stopEarly;
      stopEarly.stopAfter = 1u;
      CHECK(visit("/d", stopEarly).value() == 1u);
      CHECK(stopEarly.entries.size() == 1u);

      Collector root;
      CHECK(visit("/", root).value() == 2u); // "d" and "other"
      REQUIRE(fileSystem.mkdir("/empty").has_value());
      Collector empty;
      CHECK(visit("/empty", empty).value() == 0u);

      CHECK(failedWith(visit("/nope", root), Error::NoEntry));
      CHECK(failedWith(visit("/other", root), Error::NotDirectory));
      CHECK(failedWith(fileSystem.visitDirectory("/d", DirectoryVisitor{}), Error::InvalidArgument));
   }

   STACKED_TEST_CASE("names at the length limit are stored, listed and rejected beyond it") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      const std::string longest(kMaxNameLength, 'n');
      const std::string tooLong(kMaxNameLength + 1u, 'n');
      REQUIRE(fileSystem.writeFile(("/" + longest).c_str(), bytes("limit")).has_value());
      CHECK(readAll(fileSystem, ("/" + longest).c_str(), 8) == bytes("limit"));

      struct Names {
         std::vector<std::string> names;
         bool visit(const char *name, const FileInfo &) {
            names.emplace_back(name);
            return true;
         }
      } listed;
      REQUIRE(fileSystem.visitDirectory("/", DirectoryVisitor::create<Names, &Names::visit>(listed)).has_value());
      REQUIRE(listed.names.size() == 1u);
      CHECK(listed.names[0] == longest);

      const std::string tooLongPath = "/" + tooLong;
      CHECK(failedWith(fileSystem.open(tooLongPath.c_str(), OpenMode::Write), Error::NameTooLong));
      CHECK(failedWith(fileSystem.open(tooLongPath.c_str(), OpenMode::Read), Error::NameTooLong));
      CHECK(failedWith(fileSystem.mkdir(tooLongPath.c_str()), Error::NameTooLong));
      CHECK(failedWith(fileSystem.rename(("/" + longest).c_str(), tooLongPath.c_str()), Error::NameTooLong));
   }

   STACKED_TEST_CASE("paths the interface does not define are rejected") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      REQUIRE(fileSystem.mount().has_value());
      CHECK(failedWith(fileSystem.open("relative", OpenMode::Write), Error::InvalidArgument));
      CHECK(failedWith(fileSystem.open("", OpenMode::Write), Error::InvalidArgument));
      CHECK(failedWith(fileSystem.open("/a/../b", OpenMode::Write), Error::InvalidArgument));
      CHECK(failedWith(fileSystem.open(nullptr, OpenMode::Write), Error::InvalidArgument));
      CHECK(failedWith(fileSystem.remove(nullptr), Error::InvalidArgument));
      CHECK(failedWith(fileSystem.rename("/a", "b"), Error::InvalidArgument));
   }

   STACKED_TEST_CASE("every call on an unmounted file system fails with NotMounted") {
      Stack stack;
      FileSystemUnderTest fileSystem{ stack.device() };
      std::array<uint8_t, 4> buffer{};
      CHECK(failedWith(fileSystem.open("/a", OpenMode::Write), Error::NotMounted));
      CHECK(failedWith(fileSystem.remove("/a"), Error::NotMounted));
      CHECK(failedWith(fileSystem.rename("/a", "/b"), Error::NotMounted));
      CHECK(failedWith(fileSystem.mkdir("/a"), Error::NotMounted));
      CHECK(failedWith(fileSystem.stat("/a"), Error::NotMounted));
      CHECK(failedWith(fileSystem.freeSpace(), Error::NotMounted));
      CHECK(failedWith(fileSystem.readFile("/a", buffer), Error::NotMounted));
      CHECK(failedWith(fileSystem.writeFile("/a", buffer), Error::NotMounted));
      CHECK(fileSystem.unmount().has_value()); // idempotent
   }
}
