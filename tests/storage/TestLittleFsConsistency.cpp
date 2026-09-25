#include "FlakyReadDevice.h"
#include "PowerCutDevice.h"
#include "RamBlockDevice.h"
#include "storage/littlefs/LittleFsFileSystem.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Tests of LittleFsFileSystem::makeConsistent().
//
// littlefs completes the pending move of a cross-directory rename() lazily, in the first call that changes the volume
// after a mount. Since littlefs commit ddbfcaa7 (v2.9.1 up to v2.11.3) a read before that call does not find the entry
// that follows the moved one in the source directory: visitDirectory() does not list it and stat(), open() and
// readFile() report NoEntry for it, although nothing is wrong on the medium. The state is left by a failed rename()
// (the mount is poisoned and the caller remounts) and by a power cut between the two commits of the rename.
//
// The volume is the one of the review that found it: rename("/old", "/dir/moved") in a root directory that holds
// dir, existing, old and small. "small" follows "old" and is the entry that goes missing. The sweeps fail a device call
// at every position of the rename, remount, and check what a caller sees before any change of the volume.
//
// Nothing here relies on an assertion of littlefs: the checks look at what the public calls return and at the device
// counters, so they hold with ART_LITTLEFS_NO_ASSERT as well.

using namespace art::storage;
using namespace art::storage::test;

namespace {

// NOR-like: 16 byte read and program unit, 4 KiB erase unit, 16 blocks.
// clang-format off
constexpr Geometry kGeometry{
   .readSize = 16u, .programSize = 16u, .eraseSize = 4096u, .size = 64u * 1024u, .eraseBeforeProgram = true
};
// clang-format on

using Bytes = std::vector<uint8_t>;
using Names = std::set<std::string>;

Bytes pattern(size_t size, uint8_t seed = 0u) {
   Bytes data(size);
   for(size_t i = 0; i < size; ++i) {
      data[i] = static_cast<uint8_t>(((i * 7u) ^ (i >> 8u) ^ seed) & 0x7Fu);
   }
   return data;
}

// The content of the file, or nothing if it cannot be read (does not exist).
std::optional<Bytes> content(FileSystem &fileSystem, const char *path) {
   std::array<uint8_t, 4000> buffer{};
   const auto count = fileSystem.readFile(path, buffer);
   if(!count) {
      return std::nullopt;
   }
   return Bytes(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*count));
}

template <typename T>
bool failsWith(const std::expected<T, Error> &result, Error error) {
   return !result && result.error() == error;
}

struct Lister {
   Names names;
   bool visit(const char *name, const FileInfo &) {
      names.insert(name);
      return true;
   }
};

// The names in the directory, or nothing if it cannot be listed.
std::optional<Names> listing(FileSystem &fileSystem, const char *path) {
   Lister lister;
   if(!fileSystem.visitDirectory(path, DirectoryVisitor::create<Lister, &Lister::visit>(lister))) {
      return std::nullopt;
   }
   return lister.names;
}

// What a read-only boot sees of the files that have nothing to do with the rename. False is the upstream problem. It
// shows in four calls that share the lookup: visitDirectory() misses the entry, and stat(), open() and readFile()
// report NoEntry for it.
bool seesUnrelatedFiles(FileSystem &fileSystem) {
   const auto root = listing(fileSystem, "/");
   const auto small = fileSystem.stat("/small");
   const auto opened = fileSystem.open("/small", OpenMode::Read);
   const bool listed = root && root->contains("dir") && root->contains("existing") && root->contains("small");
   const bool intact = small && small->size == 20u && content(fileSystem, "/small") == pattern(20u, 2u);
   return listed && intact && opened.has_value();
}

// True when all four calls agree that "small" is gone, which is what the upstream problem does to a caller.
bool hidesSmallEverywhere(FileSystem &fileSystem) {
   const auto root = listing(fileSystem, "/");
   const bool statFails = failsWith(fileSystem.stat("/small"), Error::NoEntry);
   const bool openFails = failsWith(fileSystem.open("/small", OpenMode::Read), Error::NoEntry);
   return root && !root->contains("small") && statFails && openFails && !content(fileSystem, "/small").has_value();
}

// The volume, when the interrupted rename("/old", "/dir/moved") has taken effect completely or not at all, with every
// file intact.
void expectRenameSettled(FileSystem &fileSystem, bool withLastFile = false) {
   const auto root = listing(fileSystem, "/");
   const auto dir = listing(fileSystem, "/dir");
   REQUIRE(root.has_value());
   REQUIRE(dir.has_value());
   const bool applied = !root->contains("old");
   CAPTURE(applied);
   Names expectedRoot = applied ? Names{ "dir", "existing", "small" } : Names{ "dir", "existing", "old", "small" };
   if(withLastFile) {
      expectedRoot.insert("zzz");
      CHECK(content(fileSystem, "/zzz") == pattern(30u, 5u));
   }
   CHECK(*root == expectedRoot);
   CHECK(*dir == (applied ? Names{ "moved", "x" } : Names{ "x" }));
   const auto small = fileSystem.stat("/small");
   REQUIRE(small.has_value());
   CHECK(small->size == 20u);
   CHECK(content(fileSystem, "/small") == pattern(20u, 2u));
   CHECK(content(fileSystem, "/existing") == pattern(400u, 4u));
   CHECK(content(fileSystem, "/dir/x") == pattern(500u, 3u));
   CHECK(content(fileSystem, applied ? "/dir/moved" : "/old") == pattern(3000u, 1u));
   const auto gone = fileSystem.stat(applied ? "/old" : "/dir/moved");
   CHECK(failsWith(gone, Error::NoEntry));
}

// The baseline volume every step of a sweep starts from. The device stack, from the top:
// LittleFsFileSystem -> FlakyReadDevice (failing reads) -> PowerCutDevice (a cut in the write path) -> RamBlockDevice.
struct Volume {
   explicit Volume(bool withLastFile = false)
      : ram(kGeometry, backing)
      , powerCut(ram)
      , flaky(powerCut)
      , fileSystem(flaky) {
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.mkdir("/dir").has_value());
      REQUIRE(fileSystem.writeFile("/old", pattern(3000u, 1u)).has_value());
      REQUIRE(fileSystem.writeFile("/small", pattern(20u, 2u)).has_value());
      REQUIRE(fileSystem.writeFile("/dir/x", pattern(500u, 3u)).has_value());
      REQUIRE(fileSystem.writeFile("/existing", pattern(400u, 4u)).has_value());
      if(withLastFile) { // sorts behind "small", so that its id in the root directory is above the moved entry's
         REQUIRE(fileSystem.writeFile("/zzz", pattern(30u, 5u)).has_value());
      }
      REQUIRE(fileSystem.unmount().has_value());
      baseline.assign(backing.begin(), backing.end());
   }

   // The volume as it was, unmounted, with no fault armed and the device counters at zero.
   void load(const Bytes &image) {
      flaky.disarm();
      ram.clearFaults();
      powerCut.restorePower();
      if(fileSystem.isMounted()) {
         static_cast<void>(fileSystem.unmount());
      }
      std::ranges::copy(image, backing.begin());
      ram.resetCounters();
      powerCut.resetCount();
   }

   void restore() {
      load(baseline);
      REQUIRE(fileSystem.mount().has_value());
   }

   // The next boot after a failure: unmount() is the only way out of a poisoned mount and commits nothing.
   void remount() {
      REQUIRE(fileSystem.unmount().has_value());
      powerCut.restorePower();
      flaky.disarm();
      ram.clearFaults();
      REQUIRE(fileSystem.mount().has_value());
   }

   uint32_t deviceOperations() const {
      return ram.readCount() + ram.programCount() + ram.eraseCount();
   }

   std::array<uint8_t, 64u * 1024u> backing{};
   Bytes baseline;
   RamBlockDevice ram;
   PowerCutDevice powerCut;
   FlakyReadDevice flaky;
   LittleFsFileSystem<4u, 256u, 128u> fileSystem;
};

constexpr const char *kOld = "/old";
constexpr const char *kMoved = "/dir/moved";

// Read operations of a rename() that completes, and write operations (program and erase).
struct RenameCost {
   uint32_t reads;
   uint32_t writes;
};

RenameCost measureRename(Volume &volume) {
   volume.restore();
   volume.ram.resetCounters();
   volume.powerCut.resetCount();
   REQUIRE(volume.fileSystem.rename(kOld, kMoved).has_value());
   return { volume.ram.readCount(), volume.powerCut.writeOperations() };
}

// What a sweep found.
struct SweepResult {
   uint32_t positions{ 0u };
   uint32_t failed{ 0u };
   uint32_t missedEntries{ 0u };    // remounts where a read before makeConsistent() missed an unrelated file
   uint32_t hiddenEverywhere{ 0u }; // of those, remounts where the listing, stat(), open() and readFile() all miss it
};

// Fails the rename at every position of the kind, remounts like a boot, and checks that makeConsistent() settles the
// volume before anything else changes. `readFirst`: look at the volume before calling it (a read must not change the
// outcome).
template <typename Arm>
SweepResult sweepRename(Volume &volume, uint32_t positions, bool readFirst, Arm arm) {
   SweepResult result;
   result.positions = positions;
   for(uint32_t position = 0u; position < positions; ++position) {
      CAPTURE(position);
      volume.restore();
      arm(position);
      const auto renamed = volume.fileSystem.rename(kOld, kMoved);
      volume.flaky.disarm();
      volume.ram.clearFaults();
      if(renamed) { // the fault came too late to matter
         continue;
      }
      ++result.failed;
      volume.remount();
      if(readFirst && !seesUnrelatedFiles(volume.fileSystem)) {
         ++result.missedEntries;
         result.hiddenEverywhere += hidesSmallEverywhere(volume.fileSystem) ? 1u : 0u;
      }
      // No write has happened since the mount, so this is the state a boot finds.
      REQUIRE(volume.fileSystem.makeConsistent().has_value());
      CHECK(seesUnrelatedFiles(volume.fileSystem));
      expectRenameSettled(volume.fileSystem);

      // What it did is on the medium: after another boot, still no change of the volume, the listing is right.
      volume.remount();
      CHECK(seesUnrelatedFiles(volume.fileSystem));
      expectRenameSettled(volume.fileSystem);

      // Nothing is left to do: the second call does not touch the device.
      volume.ram.resetCounters();
      CHECK(volume.fileSystem.makeConsistent().has_value());
      CHECK(volume.deviceOperations() == 0u);
   }
   return result;
}

// One case, run once with a look at the volume before the call and once without.
void runReadFaultSweep(bool readFirst) {
   Volume volume;
   const RenameCost cost = measureRename(volume);
   REQUIRE(cost.reads > 100u);
   const SweepResult result =
   sweepRename(volume, cost.reads, readFirst, [&](uint32_t position) { volume.flaky.armAfter(position); });
   INFO("read fault: ", result.positions, " positions, ", result.failed, " failed, ", result.missedEntries,
        " remounts missed an unrelated file before makeConsistent()");
   CHECK(result.failed > result.positions / 2u);
   if(readFirst) {
      // The test is only worth something while it reproduces the problem. If a newer littlefs no longer does, this
      // fails: then the sweep does not prove that makeConsistent() heals anything and needs a new trigger.
      CHECK(result.missedEntries >= 10u);
      // A caller that opens or reads a record by name is told it does not exist, not only that it is not listed.
      CHECK(result.hiddenEverywhere == result.missedEntries);
   }
}

// The medium after the first read fault of the rename whose remount misses an unrelated file: the pending move, as the
// next boot finds it. Unmounted, faults disarmed.
Bytes pendingMoveImage(Volume &volume) {
   const RenameCost cost = measureRename(volume);
   for(uint32_t position = 0u; position < cost.reads; ++position) {
      volume.restore();
      volume.flaky.armAfter(position);
      const bool failed = !volume.fileSystem.rename(kOld, kMoved).has_value();
      volume.flaky.disarm();
      if(!failed) {
         continue;
      }
      volume.remount();
      if(!seesUnrelatedFiles(volume.fileSystem)) {
         REQUIRE(volume.fileSystem.unmount().has_value());
         return Bytes(volume.backing.begin(), volume.backing.end());
      }
   }
   FAIL("no read position of the rename leaves the pending move that hides an entry");
   return {};
}

// Fails one device call at every position of makeConsistent() on the pending move, and checks that the failure poisons
// the mount and that the next boot can finish the job. `positions` is the number of calls to sweep; `arm` arms the
// fault that hits the call at the given position.
template <typename Arm>
uint32_t sweepMakeConsistent(Volume &volume, const Bytes &pending, uint32_t positions, bool withLastFile, Arm arm) {
   uint32_t failed = 0u;
   for(uint32_t position = 0u; position < positions; ++position) {
      CAPTURE(position);
      volume.load(pending);
      REQUIRE(volume.fileSystem.mount().has_value());
      arm(position);
      const auto result = volume.fileSystem.makeConsistent();
      volume.flaky.disarm();
      volume.ram.clearFaults();
      if(result) { // a fault that littlefs absorbed: the volume must be settled then
         expectRenameSettled(volume.fileSystem, withLastFile);
         continue;
      }
      ++failed;
      CHECK(failsWith(result, Error::Io));
      // Poisoned: littlefs cleared the move in memory before the commit that failed.
      CHECK(volume.fileSystem.isMounted());
      CHECK(failsWith(volume.fileSystem.makeConsistent(), Error::NeedsRemount));
      CHECK(failsWith(volume.fileSystem.stat("/small"), Error::NeedsRemount));
      CHECK(!listing(volume.fileSystem, "/").has_value());

      // The medium is still the pending move or the finished one, and the next boot completes it.
      volume.remount();
      REQUIRE(volume.fileSystem.makeConsistent().has_value());
      expectRenameSettled(volume.fileSystem, withLastFile);
      volume.remount();
      expectRenameSettled(volume.fileSystem, withLastFile);
      volume.ram.resetCounters();
      CHECK(volume.fileSystem.makeConsistent().has_value());
      CHECK(volume.deviceOperations() == 0u);
   }
   return failed;
}

// The boot sequence the header documents: mount, try the repair once, and if that fails go on without it after a
// remount (the failure poisoned the mount). Never loops. Returns whether the repair worked.
bool bootWithBoundedRepair(LittleFsFileSystemBase &fileSystem) {
   REQUIRE(fileSystem.mount().has_value());
   if(fileSystem.makeConsistent()) {
      return true;
   }
   REQUIRE(fileSystem.unmount().has_value());
   REQUIRE(fileSystem.mount().has_value());
   return false;
}

// The volume of the sweeps, with the files that have nothing to do with the operation under test intact.
void expectBaseFilesIntact(FileSystem &fileSystem) {
   const auto root = listing(fileSystem, "/");
   REQUIRE(root.has_value());
   CHECK(root->contains("dir"));
   CHECK(root->contains("existing"));
   CHECK(content(fileSystem, "/existing") == pattern(400u, 4u));
   CHECK(content(fileSystem, "/dir/x") == pattern(500u, 3u));
   const auto info = fileSystem.stat("/dir");
   REQUIRE(info.has_value());
   CHECK(info->type == EntryType::Directory);
}

} // namespace

TEST_SUITE("LittleFsFileSystem makeConsistent") {
   TEST_CASE("a read after the remount of a failed rename misses an unrelated file until makeConsistent() ran") {
      runReadFaultSweep(true);
   }

   TEST_CASE("makeConsistent() settles the volume without a look at it first") {
      runReadFaultSweep(false);
   }

   TEST_CASE("makeConsistent() settles a rename that a power cut interrupted") {
      Volume volume;
      const RenameCost cost = measureRename(volume);
      REQUIRE(cost.writes == 2u); // the two commits of the rename
      uint32_t failed = 0u;
      uint32_t missed = 0u;
      for(const size_t tornBytes : { size_t{ 0u }, size_t{ 1u }, size_t{ 8u }, size_t{ 15u } }) {
         CAPTURE(tornBytes);
         const SweepResult result = sweepRename(volume, cost.writes, true, [&](uint32_t position) {
            volume.powerCut.armCut(position + 1u, tornBytes);
         });
         failed += result.failed;
         missed += result.missedEntries;
      }
      INFO("power cut: ", failed, " cuts, ", missed, " remounts missed an unrelated file before makeConsistent()");
      CHECK(failed > cost.writes);
      CHECK(missed >= 2u);
   }

   TEST_CASE("makeConsistent() on a consistent volume neither reads nor writes") {
      Volume volume;
      volume.restore();
      volume.ram.resetCounters();
      CHECK(volume.fileSystem.makeConsistent().has_value());
      CHECK(volume.deviceOperations() == 0u);

      // After use as well: files written, a rename inside a mount, a directory removed.
      REQUIRE(volume.fileSystem.writeFile("/new", pattern(700u, 8u)).has_value());
      REQUIRE(volume.fileSystem.rename("/old", "/dir/moved").has_value());
      REQUIRE(volume.fileSystem.remove("/existing").has_value());
      volume.ram.resetCounters();
      CHECK(volume.fileSystem.makeConsistent().has_value());
      CHECK(volume.deviceOperations() == 0u);
      volume.remount();
      volume.ram.resetCounters();
      CHECK(volume.fileSystem.makeConsistent().has_value());
      CHECK(volume.deviceOperations() == 0u);
      CHECK(content(volume.fileSystem, "/dir/moved") == pattern(3000u, 1u));
   }

   TEST_CASE("makeConsistent() is refused on an unmounted file system and on a poisoned mount, before the device") {
      Volume volume;
      CHECK(failsWith(volume.fileSystem.makeConsistent(), Error::NotMounted));

      volume.restore();
      volume.flaky.armAfter(0u);
      REQUIRE(failsWith(volume.fileSystem.rename(kOld, kMoved), Error::Timeout));
      volume.flaky.disarm();
      volume.ram.resetCounters();
      CHECK(failsWith(volume.fileSystem.makeConsistent(), Error::NeedsRemount));
      CHECK(volume.deviceOperations() == 0u);
      CHECK(volume.fileSystem.isMounted());

      volume.remount();
      CHECK(volume.fileSystem.makeConsistent().has_value());
      REQUIRE(volume.fileSystem.unmount().has_value());
      volume.ram.resetCounters();
      CHECK(failsWith(volume.fileSystem.makeConsistent(), Error::NotMounted));
      CHECK(volume.deviceOperations() == 0u);
   }

   TEST_CASE("makeConsistent() with files open") {
      SUBCASE("a writer and a reader on a consistent volume: nothing to do, the writer keeps its data") {
         Volume volume;
         volume.restore();
         auto writer = volume.fileSystem.open("/dir/x", OpenMode::Append);
         auto reader = volume.fileSystem.open("/small", OpenMode::Read);
         REQUIRE(writer.has_value());
         REQUIRE(reader.has_value());
         REQUIRE(writer->write(pattern(10u, 9u)).has_value()); // stays in the file cache, not synced
         volume.ram.resetCounters();
         CHECK(volume.fileSystem.makeConsistent().has_value());
         CHECK(volume.deviceOperations() == 0u);
         CHECK(writer->close().has_value());
         std::array<uint8_t, 32> buffer{};
         const auto count = reader->read(buffer);
         REQUIRE(count.has_value());
         CHECK(Bytes(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*count)) == pattern(20u, 2u));
         CHECK(reader->close().has_value());
         Bytes expected = pattern(500u, 3u);
         const Bytes tail = pattern(10u, 9u);
         expected.insert(expected.end(), tail.begin(), tail.end());
         CHECK(content(volume.fileSystem, "/dir/x") == expected);
      }

      SUBCASE("readers on the pending move: their entries keep working when the move is deleted below them") {
         Volume volume(true);
         const Bytes pending = pendingMoveImage(volume);
         volume.load(pending);
         REQUIRE(volume.fileSystem.mount().has_value());
         REQUIRE_FALSE(seesUnrelatedFiles(volume.fileSystem));
         // "existing" sorts before the moved entry, "zzz" behind it (the repair lowers its id in the directory), and
         // "dir/x" is in another directory. Every file is read in two parts, with makeConsistent() in between.
         struct Reader {
            const char *path;
            Bytes expected;
            File file;
         };
         std::vector<Reader> readers;
         const std::pair<const char *, Bytes> files[] = { { "/existing", pattern(400u, 4u) },
                                                          { "/zzz", pattern(30u, 5u) },
                                                          { "/dir/x", pattern(500u, 3u) } };
         for(const auto &[path, expected] : files) {
            auto file = volume.fileSystem.open(path, OpenMode::Read);
            REQUIRE(file.has_value());
            readers.push_back({ path, expected, std::move(*file) });
         }
         const auto readSome = [](Reader &reader, size_t count) {
            Bytes data(count);
            const auto got = reader.file.read(data);
            REQUIRE(got.has_value());
            data.resize(*got);
            return data;
         };
         std::vector<Bytes> firstParts;
         for(Reader &reader : readers) {
            firstParts.push_back(readSome(reader, 10u));
         }
         CHECK(volume.fileSystem.makeConsistent().has_value());
         for(size_t index = 0u; index < readers.size(); ++index) {
            CAPTURE(readers[index].path);
            Bytes all = firstParts[index];
            const Bytes rest = readSome(readers[index], 1000u);
            all.insert(all.end(), rest.begin(), rest.end());
            CHECK(all == readers[index].expected);
            CHECK(readers[index].file.close().has_value());
         }
         expectRenameSettled(volume.fileSystem, true);
      }

      SUBCASE("a writer on the pending move: opening it for writing already finished the job") {
         Volume volume(true);
         const Bytes pending = pendingMoveImage(volume);
         volume.load(pending);
         REQUIRE(volume.fileSystem.mount().has_value());
         auto writer = volume.fileSystem.open("/dir/x", OpenMode::Append);
         REQUIRE(writer.has_value());
         REQUIRE(writer->write(pattern(10u, 9u)).has_value());
         CHECK(seesUnrelatedFiles(volume.fileSystem));
         volume.ram.resetCounters();
         CHECK(volume.fileSystem.makeConsistent().has_value());
         CHECK(volume.deviceOperations() == 0u);
         CHECK(writer->close().has_value());
         Bytes expected = pattern(500u, 3u);
         const Bytes tail = pattern(10u, 9u);
         expected.insert(expected.end(), tail.begin(), tail.end());
         CHECK(content(volume.fileSystem, "/dir/x") == expected);
         CHECK(content(volume.fileSystem, "/zzz") == pattern(30u, 5u));
         CHECK(content(volume.fileSystem, "/small") == pattern(20u, 2u));
      }
   }

   TEST_CASE("a failed makeConsistent() poisons the mount and the next boot completes it") {
      Volume volume;
      const Bytes pending = pendingMoveImage(volume);
      volume.load(pending);
      REQUIRE(volume.fileSystem.mount().has_value());
      volume.ram.resetCounters();
      volume.powerCut.resetCount();
      REQUIRE(volume.fileSystem.makeConsistent().has_value());
      const uint32_t operations = volume.deviceOperations();
      const uint32_t writes = volume.powerCut.writeOperations();
      REQUIRE(writes >= 1u);

      SUBCASE("a device call fails, one after the other") {
         const uint32_t failed = sweepMakeConsistent(volume, pending, operations, false, [&](uint32_t position) {
            volume.ram.failNthOperation(position + 1u);
         });
         INFO("device call fault: ", failed, " of ", operations, " calls of makeConsistent() failed it");
         CHECK(failed == operations);
      }

      SUBCASE("the power is cut in a write, with the write torn after n bytes") {
         uint32_t failed = 0u;
         for(const size_t tornBytes : { size_t{ 0u }, size_t{ 1u }, size_t{ 8u }, size_t{ 15u } }) {
            CAPTURE(tornBytes);
            failed += sweepMakeConsistent(volume, pending, writes, false,
                                          [&](uint32_t position) { volume.powerCut.armCut(position + 1u, tornBytes); });
         }
         CHECK(failed == 4u * writes);
      }
   }

   TEST_CASE("a failed makeConsistent() reports the error of the device call that failed it") {
      Volume volume;
      volume.load(pendingMoveImage(volume));
      REQUIRE(volume.fileSystem.mount().has_value());
      volume.flaky.armAfter(0u);
      CHECK(failsWith(volume.fileSystem.makeConsistent(), Error::Timeout));
      volume.flaky.disarm();
      CHECK(failsWith(volume.fileSystem.makeConsistent(), Error::NeedsRemount));
   }

   TEST_CASE("a medium that cannot be written: the bounded boot sequence keeps reading the volume") {
      Volume volume;
      const Bytes pending = pendingMoveImage(volume);
      volume.load(pending);

      // The first write of the first boot kills the power for good; the medium is read only from then on.
      REQUIRE(volume.fileSystem.mount().has_value());
      volume.powerCut.armCut(1u, 0u);
      REQUIRE(failsWith(volume.fileSystem.makeConsistent(), Error::Io));
      REQUIRE(failsWith(volume.fileSystem.stat("/existing"), Error::NeedsRemount)); // why the caller must not loop
      REQUIRE(volume.fileSystem.unmount().has_value());

      for(uint32_t boot = 0u; boot < 3u; ++boot) {
         CAPTURE(boot);
         CHECK_FALSE(bootWithBoundedRepair(volume.fileSystem));
         // Everything but the entry the upstream problem hides is readable, the volume is not lost.
         expectBaseFilesIntact(volume.fileSystem);
         REQUIRE(volume.fileSystem.unmount().has_value());
      }

      // The medium works again (or space was freed): the repair, tried once more, settles the volume.
      volume.powerCut.restorePower();
      CHECK(bootWithBoundedRepair(volume.fileSystem));
      expectRenameSettled(volume.fileSystem);
   }

   TEST_CASE("makeConsistent() after a power cut in rmdir: cost and result with an orphan") {
      // Removing a directory commits the parent first, with the orphan count raised in the gstate, and drops the
      // directory's metadata pair afterwards. A power cut in between leaves an orphan, which littlefs finds by
      // traversing the whole metadata tree in two passes in the first change. That is not bounded by the move of a
      // rename, so this measures the cost and checks the result.
      Volume volume;
      volume.restore();
      REQUIRE(volume.fileSystem.mkdir("/gone").has_value());
      REQUIRE(volume.fileSystem.unmount().has_value());
      const Bytes withGone(volume.backing.begin(), volume.backing.end());
      volume.load(withGone);
      REQUIRE(volume.fileSystem.mount().has_value());
      REQUIRE(volume.fileSystem.remove("/gone").has_value());
      const uint32_t writes = volume.powerCut.writeOperations();
      REQUIRE(writes >= 2u);

      uint32_t cuts = 0u;
      uint32_t withWork = 0u;
      uint32_t mostOperations = 0u;
      uint32_t mostWrites = 0u;
      for(const size_t tornBytes : { size_t{ 0u }, size_t{ 8u } }) {
         for(uint32_t position = 0u; position < writes; ++position) {
            CAPTURE(tornBytes);
            CAPTURE(position);
            volume.load(withGone);
            REQUIRE(volume.fileSystem.mount().has_value());
            volume.powerCut.armCut(position + 1u, tornBytes);
            CHECK_FALSE(volume.fileSystem.remove("/gone").has_value());
            volume.remount();
            ++cuts;
            volume.ram.resetCounters();
            volume.powerCut.resetCount();
            REQUIRE(volume.fileSystem.makeConsistent().has_value());
            if(volume.deviceOperations() > 0u) {
               ++withWork;
               mostOperations = std::max(mostOperations, volume.deviceOperations());
               mostWrites = std::max(mostWrites, volume.powerCut.writeOperations());
            }
            expectBaseFilesIntact(volume.fileSystem);
            const auto root = listing(volume.fileSystem, "/");
            REQUIRE(root.has_value());
            CHECK(root->size() == (root->contains("gone") ? 5u : 4u)); // removed completely, or not at all

            // The repair is on the medium, and the volume takes the call again.
            volume.remount();
            volume.ram.resetCounters();
            CHECK(volume.fileSystem.makeConsistent().has_value());
            CHECK(volume.deviceOperations() == 0u);
            CHECK(volume.fileSystem.mkdir("/after").has_value());
         }
      }
      INFO("rmdir sweep: ", cuts, " cuts, ", withWork, " left work, at most ", mostOperations,
           " device operations and ", mostWrites, " writes");
      CHECK(withWork >= 1u); // the test measures an orphan, or it measures nothing
   }
}
