#include "FlakyReadDevice.h"
#include "RamBlockDevice.h"
#include "storage/littlefs/LittleFsFileSystem.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <doctest/doctest.h>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// littlefs keeps data for a block it is about to program in its program cache, and asserts that the cache is empty
// before it starts on another block. A write that fails on a device read part way (a metadata commit reads back what
// it programmed) leaves the cache dirty, so the next write on the same mount used to abort in lfs_bd_prog(). These
// tests inject a read failure at every read of one operation in turn, let the device recover and require the retry
// on the same mount to succeed with the right content. littlefs asserts must be enabled for that (the native
// default); with ART_LITTLEFS_NO_ASSERT the abort is compiled out and only the content checks remain.

using namespace art::storage;
using namespace art::storage::test;

namespace art::storage {

// Reaches into the binding to see the state of littlefs that no public call shows.
struct LittleFsFileSystemTestAccess {
   // A littlefs call that finishes leaves its program cache empty: no block, and every byte 0xFF (lfs_cache_zero()).
   // One that fails part way does not, and it is the binding's resetProgramCache() that restores it. The next write
   // then aborts in an assertion of littlefs, or with the assertions compiled out (the firmware default) programs
   // the stale bytes in front of its data. Nothing of that is visible through the public calls in every case, so
   // this reads the state itself; it is the check that works without LFS_ASSERT.
   static bool programCacheEmpty(const LittleFsFileSystemBase &fileSystem) {
      return fileSystem.lfs_.pcache.block == static_cast<lfs_block_t>(-1) &&
             std::ranges::all_of(fileSystem.buffers_.programCache, [](uint8_t byte) { return byte == 0xFFu; });
   }
};

} // namespace art::storage

namespace {

// NOR-like: 16 byte read and program unit, 4 KiB erase unit, 16 blocks.
// clang-format off
constexpr Geometry kGeometry{
   .readSize = 16u, .programSize = 16u, .eraseSize = 4096u, .size = 64u * 1024u, .eraseBeforeProgram = true
};
// clang-format on

// Upper bound of the sweep; every scenario finishes long before it.
constexpr uint32_t kMaxReads = 20000u;
// A commit that compacts a directory reads the device thousands of times (it scans the volume for a free block).
constexpr uint32_t kCompactionStride = 47u;

std::vector<uint8_t> pattern(size_t size, uint8_t seed = 0u) {
   std::vector<uint8_t> data(size);
   for(size_t i = 0; i < size; ++i) {
      data[i] = static_cast<uint8_t>(((i * 7u) ^ (i >> 8u) ^ seed) & 0x7Fu);
   }
   return data;
}

std::vector<uint8_t> readBack(FileSystem &fileSystem, const char *path) {
   std::array<uint8_t, 8000> buffer{};
   const auto count = fileSystem.readFile(path, buffer);
   REQUIRE(count.has_value());
   return { buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*count) };
}

// A volume with a few files, saved as a baseline image that every sweep step starts from.
struct Fixture {
   // `churn` create and remove cycles in the root directory move the end of its metadata log, so that the
   // operation under test can start right before the commit that compacts it.
   explicit Fixture(uint32_t churn = 0u)
      : ram(kGeometry, backing)
      , flaky(ram)
      , fileSystem(flaky) {
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.mkdir("/dir").has_value());
      REQUIRE(fileSystem.writeFile("/old", pattern(3000u, 1u)).has_value());
      REQUIRE(fileSystem.writeFile("/small", pattern(20u, 2u)).has_value());
      REQUIRE(fileSystem.writeFile("/dir/x", pattern(500u, 3u)).has_value());
      REQUIRE(fileSystem.mkdir("/empty").has_value());
      for(uint32_t cycle = 0u; cycle < churn; ++cycle) {
         REQUIRE(fileSystem.writeFile("/churn", pattern(20u, 22u)).has_value());
         REQUIRE(fileSystem.remove("/churn").has_value());
      }
      REQUIRE(fileSystem.unmount().has_value());
      baseline = backing;
   }

   // Back to the baseline volume. The read cache holds the block that mount() read last, and an operation that finds
   // what it needs there does not read the device at all, so it would never see an injected failure. Reading a file
   // that lives elsewhere replaces it.
   void restore() {
      if(fileSystem.isMounted()) {
         static_cast<void>(fileSystem.unmount());
      }
      backing = baseline;
      REQUIRE(fileSystem.mount().has_value());
      std::array<uint8_t, 600> buffer{};
      REQUIRE(fileSystem.readFile("/dir/x", buffer).has_value());
   }

   std::array<uint8_t, 64u * 1024u> backing{};
   std::array<uint8_t, 64u * 1024u> baseline{};
   RamBlockDevice ram;
   FlakyReadDevice flaky;
   LittleFsFileSystem<> fileSystem;
};

// The number of churn cycles after which the next commit of the root directory compacts it. Compaction is the one
// step of a metadata commit that reads the device while the program cache holds the block it builds.
uint32_t churnUntilCompaction() {
   Fixture fixture;
   const uint32_t erasesBefore = fixture.ram.eraseCount();
   REQUIRE(fixture.fileSystem.mount().has_value());
   for(uint32_t cycle = 1u; cycle < 1000u; ++cycle) {
      REQUIRE(fixture.fileSystem.writeFile("/churn", pattern(20u, 22u)).has_value());
      REQUIRE(fixture.fileSystem.remove("/churn").has_value());
      if(fixture.ram.eraseCount() != erasesBefore) {
         return cycle;
      }
   }
   FAIL("the root directory never compacted");
   return 0u;
}

// Writes to the root directory and to a subdirectory, which are metadata pairs on different blocks. littlefs asserts
// when it programs a block while its program cache still holds another one, so this is the first thing after a
// failure that notices a dirty cache. Repeating the failed operation alone can miss it, when that lands in the block
// the cache holds.
void writeElsewhere(FileSystem &fileSystem) {
   const auto inRoot = pattern(300u, 20u);
   const auto inDirectory = pattern(600u, 21u);
   REQUIRE(fileSystem.writeFile("/dir/probe", inDirectory).has_value());
   REQUIRE(fileSystem.writeFile("/probe", inRoot).has_value());
   CHECK(readBack(fileSystem, "/dir/probe") == inDirectory);
   CHECK(readBack(fileSystem, "/probe") == inRoot);
}

// Runs `attempt` with a read failing at read #k for k = 0, 1, ... on a fresh copy of the baseline volume, and
// `recover` on the same mount afterwards. `attempt` returns true if it passed completely, which ends the sweep
// because later steps would pass too. `recover` gets the outcome, so that it can skip what already happened.
// `stride` skips positions for a scenario that reads the device thousands of times. `probe` makes the sweep write to
// other directories before `recover` runs (see writeElsewhere()); a scenario that must remount first turns it off.
void sweep(Fixture &fixture,
           const std::function<bool(FileSystem &)> &attempt,
           const std::function<void(FileSystem &, bool)> &recover,
           uint32_t stride = 1u,
           bool probe = true) {
   uint32_t failedSteps = 0u;
   bool completed = false;
   for(uint32_t failAfter = 0u; failAfter < kMaxReads && !completed; failAfter += stride) {
      CAPTURE(failAfter);
      fixture.restore();
      fixture.flaky.armAfter(failAfter);
      completed = attempt(fixture.fileSystem);
      fixture.flaky.disarm();
      failedSteps += completed ? 0u : 1u;
      CHECK(LittleFsFileSystemTestAccess::programCacheEmpty(fixture.fileSystem));
      if(probe) {
         writeElsewhere(fixture.fileSystem);
      }
      recover(fixture.fileSystem, completed);
   }
   CHECK(completed);
   CHECK(failedSteps > 0u); // otherwise the sweep never injected anything
}

// A failed rename, remove or mkdir may have taken effect nevertheless: littlefs commits first and can still fail on
// a read that follows. The retry then reports that the work is already done, which is as good as success.
void retryOrAlreadyDone(const std::expected<void, Error> &retry, Error alreadyDone) {
   if(!retry) {
      CHECK((retry.error() == alreadyDone));
   }
}

// Writes the file through a handle in two chunks and closes it; false on the first error.
bool writeThroughHandle(FileSystem &fileSystem, const char *path, OpenMode mode, std::span<const uint8_t> data) {
   auto file = fileSystem.open(path, mode);
   if(!file) {
      return false;
   }
   const size_t half = data.size() / 2u;
   if(!file->write(data.first(half)) || !file->write(data.subspan(half))) {
      static_cast<void>(file->close());
      return false;
   }
   return file->close().has_value();
}

bool contains(std::initializer_list<const char *> names, const char *name) {
   return std::ranges::any_of(names, [name](const char *candidate) { return std::string_view(candidate) == name; });
}

// The recovery of the write scenarios: the same write again, then unrelated writes, then (unless open files must
// survive) a remount, all on the volume that saw the failure. The old content of the path may be lost by the failed
// attempt, the new must not be.
void expectWritesWork(FileSystem &fileSystem,
                      const char *path,
                      std::span<const uint8_t> data,
                      bool remount = true,
                      std::initializer_list<const char *> changed = {}) {
   REQUIRE(fileSystem.writeFile(path, data).has_value());
   CHECK(readBack(fileSystem, path) == std::vector<uint8_t>(data.begin(), data.end()));
   const auto other = pattern(2500u, 9u);
   REQUIRE(fileSystem.writeFile("/other", other).has_value());
   REQUIRE(fileSystem.mkdir("/dir/sub").has_value());
   CHECK(readBack(fileSystem, "/other") == other);
   // The files of the baseline the scenario does not touch.
   const auto untouched = [&](const char *name) { return std::string_view(name) != path && !contains(changed, name); };
   if(untouched("/old")) {
      CHECK(readBack(fileSystem, "/old") == pattern(3000u, 1u));
   }
   if(untouched("/small")) {
      CHECK(readBack(fileSystem, "/small") == pattern(20u, 2u));
   }
   if(untouched("/dir/x")) {
      CHECK(readBack(fileSystem, "/dir/x") == pattern(500u, 3u));
   }
   if(!remount) {
      return;
   }

   REQUIRE(fileSystem.unmount().has_value());
   REQUIRE(fileSystem.mount().has_value());
   CHECK(readBack(fileSystem, path) == std::vector<uint8_t>(data.begin(), data.end()));
   CHECK(readBack(fileSystem, "/other") == other);
}

} // namespace

TEST_SUITE("LittleFsFileSystem program cache after a failed device read") {

   TEST_CASE("writeFile of a new file can be retried after a failed read at any point") {
      Fixture fixture;
      for(const size_t size : { size_t{ 20u }, size_t{ 3000u } }) { // inline in the metadata, and in blocks
         CAPTURE(size);
         const auto data = pattern(size, 4u);
         sweep(
         fixture, [&](FileSystem &fs) { return fs.writeFile("/new", data).has_value(); },
         [&](FileSystem &fs, bool) { expectWritesWork(fs, "/new", data); });
      }
   }

   TEST_CASE("writeFile over an existing file can be retried after a failed read at any point") {
      Fixture fixture;
      for(const size_t size : { size_t{ 20u }, size_t{ 5000u } }) {
         CAPTURE(size);
         const auto data = pattern(size, 5u);
         sweep(
         fixture, [&](FileSystem &fs) { return fs.writeFile("/old", data).has_value(); },
         [&](FileSystem &fs, bool) { expectWritesWork(fs, "/old", data); });
      }
   }

   TEST_CASE("File::write and close can be retried after a failed read at any point") {
      Fixture fixture;
      for(const OpenMode mode : { OpenMode::Write, OpenMode::Append, OpenMode::ReadWrite }) {
         CAPTURE(static_cast<int>(mode));
         const auto data = pattern(6000u, 6u);
         sweep(
         fixture, [&](FileSystem &fs) { return writeThroughHandle(fs, "/old", mode, data); },
         [&](FileSystem &fs, bool) {
            REQUIRE(fs.writeFile("/probe", pattern(3000u, 7u)).has_value());
            expectWritesWork(fs, "/copy", pattern(4000u, 8u), true, { "/old" });
         });
      }
   }

   TEST_CASE("File::sync can be retried after a failed read at any point") {
      // The data is written before the failure is armed, so the failing reads belong to the flush and the
      // commit in sync(). The handle stays errored (its sync keeps reporting the failure); the retry is a new one.
      for(const size_t size : { size_t{ 20u }, size_t{ 100u }, size_t{ 3000u } }) {
         CAPTURE(size);
         Fixture fixture;
         const auto data = pattern(size, 10u);
         bool completed = false;
         uint32_t failedSteps = 0u;
         for(uint32_t failAfter = 0u; failAfter < kMaxReads && !completed; ++failAfter) {
            CAPTURE(failAfter);
            fixture.restore();
            auto file = fixture.fileSystem.open("/old", OpenMode::Write);
            REQUIRE(file.has_value());
            REQUIRE(file->write(data).has_value());
            fixture.flaky.armAfter(failAfter);
            completed = file->sync().has_value();
            fixture.flaky.disarm();
            failedSteps += completed ? 0u : 1u;
            writeElsewhere(fixture.fileSystem);
            static_cast<void>(file->close());
            expectWritesWork(fixture.fileSystem, "/old", data);
         }
         CHECK(completed);
         CHECK(failedSteps > 0u);
      }
   }

   TEST_CASE("rename within a directory can be retried after a failed read at any point") {
      Fixture fixture;
      // A new name, one that replaces a file, and the same in a subdirectory.
      using Rename = std::pair<const char *, const char *>;
      const Rename renames[] = { { "/old", "/renamed" }, { "/old", "/small" }, { "/dir/x", "/dir/y" } };
      for(const auto &[from, to] : renames) {
         CAPTURE(std::string(from));
         CAPTURE(std::string(to));
         fixture.restore();
         const auto content = readBack(fixture.fileSystem, from);
         sweep(
         fixture, [&, to = to, from = from](FileSystem &fs) { return fs.rename(from, to).has_value(); },
         [&, to = to, from = from](FileSystem &fs, bool completed) {
            if(!completed) {
               retryOrAlreadyDone(fs.rename(from, to), Error::NoEntry);
            }
            CHECK(readBack(fs, to) == content);
            CHECK((fs.stat(from).error() == Error::NoEntry));
            expectWritesWork(fs, "/after", pattern(3000u, 13u), true, { from, to });
         });
      }
   }

   // A rename into another directory commits twice. A read that fails in between leaves the pending move of littlefs
   // out of step with the medium, and any call on the same mount is then undefined (the repeat can even abort). That
   // does not depend on the program cache. The mount has to be remounted before anything else; littlefs then
   // completes or undoes the move like after a power cut, which is what this test requires at every position.
   TEST_CASE("a rename into another directory leaves a consistent volume after a failed read and a remount") {
      Fixture fixture;
      const auto content = pattern(3000u, 1u);
      sweep(
      fixture, [](FileSystem &fs) { return fs.rename("/old", "/dir/moved").has_value(); },
      [&](FileSystem &fs, bool completed) {
         REQUIRE(fs.unmount().has_value());
         REQUIRE(fs.mount().has_value());
         const bool atOld = fs.stat("/old").has_value();
         const bool atNew = fs.stat("/dir/moved").has_value();
         CHECK(atOld != atNew); // exactly one of them; the rename is atomic
         CHECK((completed ? atNew : true));
         CHECK(readBack(fs, atNew ? "/dir/moved" : "/old") == content);
         expectWritesWork(fs, "/after", pattern(3000u, 26u), true, { "/old", "/dir/moved" });
      },
      1u, false);
   }

   TEST_CASE("remove can be retried after a failed read at any point") {
      Fixture fixture;
      for(const char *path : { "/old", "/small", "/dir/x", "/empty" }) {
         CAPTURE(std::string(path));
         sweep(
         fixture, [&](FileSystem &fs) { return fs.remove(path).has_value(); },
         [&](FileSystem &fs, bool completed) {
            if(!completed) {
               retryOrAlreadyDone(fs.remove(path), Error::NoEntry);
            }
            CHECK((fs.stat(path).error() == Error::NoEntry));
            expectWritesWork(fs, "/after", pattern(3000u, 14u), true, { path });
         });
      }
   }

   TEST_CASE("mkdir can be retried after a failed read at any point") {
      Fixture fixture;
      sweep(
      fixture, [](FileSystem &fs) { return fs.mkdir("/made").has_value(); },
      [](FileSystem &fs, bool completed) {
         if(!completed) {
            retryOrAlreadyDone(fs.mkdir("/made"), Error::Exists);
         }
         CHECK(fs.stat("/made").value().type == EntryType::Directory);
         expectWritesWork(fs, "/made/file", pattern(3000u, 15u));
      });
   }

   TEST_CASE("unmount after a failed write and a remount leave a usable volume") {
      Fixture fixture;
      const auto data = pattern(3000u, 16u);
      bool completed = false;
      for(uint32_t failAfter = 0u; failAfter < kMaxReads && !completed; ++failAfter) {
         CAPTURE(failAfter);
         fixture.restore();
         fixture.flaky.armAfter(failAfter);
         completed = fixture.fileSystem.writeFile("/new", data).has_value();
         fixture.flaky.disarm();
         writeElsewhere(fixture.fileSystem);
         REQUIRE(fixture.fileSystem.unmount().has_value());
         REQUIRE(fixture.fileSystem.mount().has_value());
         expectWritesWork(fixture.fileSystem, "/new", data);
      }
      CHECK(completed);
   }

   TEST_CASE("a failed operation leaves the pending data of another open file alone") {
      // The program cache is reset, the caches of files that are being written are not: their pending data is
      // only there.
      Fixture fixture;
      const auto pending = pattern(1000u, 17u); // its tail stays in the file cache until close()
      const std::vector<std::pair<const char *, std::function<bool(FileSystem &)>>> operations{
         { "mkdir", [](FileSystem &fs) { return fs.mkdir("/made").has_value(); } },
         { "remove", [](FileSystem &fs) { return fs.remove("/small").has_value(); } },
         { "rename", [](FileSystem &fs) { return fs.rename("/small", "/renamed").has_value(); } },
         { "writeFile", [](FileSystem &fs) { return fs.writeFile("/new", pattern(3000u, 18u)).has_value(); } },
      };
      for(const auto &[name, operation] : operations) {
         CAPTURE(std::string(name));
         bool completed = false;
         for(uint32_t failAfter = 0u; failAfter < kMaxReads && !completed; ++failAfter) {
            CAPTURE(failAfter);
            fixture.restore();
            auto writer = fixture.fileSystem.open("/pending", OpenMode::Write);
            REQUIRE(writer.has_value());
            REQUIRE(writer->write(pending).has_value());

            fixture.flaky.armAfter(failAfter);
            completed = operation(fixture.fileSystem);
            fixture.flaky.disarm();

            REQUIRE(writer->write(pattern(10u, 19u)).has_value());
            REQUIRE(writer->close().has_value());
            auto expected = pending;
            const auto tail = pattern(10u, 19u);
            expected.insert(expected.end(), tail.begin(), tail.end());
            CHECK(readBack(fixture.fileSystem, "/pending") == expected);
         }
         CHECK(completed);
      }
   }

   TEST_CASE("a commit that compacts a directory can be retried after a failed read at any point") {
      const uint32_t compactingCycle = churnUntilCompaction();
      const std::vector<std::pair<const char *, std::function<bool(FileSystem &)>>> operations{
         { "create", [](FileSystem &fs) { return fs.writeFile("/new", pattern(20u, 23u)).has_value(); } },
         { "mkdir", [](FileSystem &fs) { return fs.mkdir("/made").has_value(); } },
         { "remove", [](FileSystem &fs) { return fs.remove("/small").has_value(); } },
         { "rename", [](FileSystem &fs) { return fs.rename("/small", "/renamed").has_value(); } },
         { "handle", [](FileSystem &fs) { return writeThroughHandle(fs, "/small", OpenMode::Write, pattern(50u)); } },
      };
      const auto recover = [](FileSystem &fs, bool) {
         expectWritesWork(fs, "/after", pattern(3000u, 25u), true, { "/small" });
      };
      for(const auto &[name, operation] : operations) {
         for(uint32_t margin = 1u; margin <= 2u; ++margin) {
            CAPTURE(std::string(name));
            CAPTURE(margin);
            Fixture fixture(compactingCycle - margin);
            sweep(fixture, operation, recover, kCompactionStride);
         }
      }
   }
}
