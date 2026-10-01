#include "FlakyReadDevice.h"
#include "RamBlockDevice.h"
#include "storage/littlefs/LittleFsFileSystem.h"
#include "utils/compat/span.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// littlefs keeps state in memory that a failed call can leave out of step with the medium, and the binding cannot
// repair it: the pending move of a rename into another directory, and (issue #15) whatever loses the data of a file
// that is open for writing when another call fails. The binding then refuses every call with Error::NeedsRemount
// until unmount() and mount(). These tests fail a device call at every position of one call at a time and check:
// - the mount is poisoned exactly in the two situations, and in no other,
// - every public method and file hook refuses, and unmount() is safe and does not commit,
// - what a remount leaves behind (nothing inconsistent, and of an open writer what it had synced).
// Nothing here relies on an assertion of littlefs: the checks look at what the public calls return and at the medium,
// so they hold with ART_LITTLEFS_NO_ASSERT as well.

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

Bytes pattern(size_t size, uint8_t seed = 0u) {
   Bytes data(size);
   for(size_t i = 0; i < size; ++i) {
      data[i] = static_cast<uint8_t>(((i * 7u) ^ (i >> 8u) ^ seed) & 0x7Fu);
   }
   return data;
}

Bytes joined(std::initializer_list<Bytes> parts) {
   Bytes all;
   for(const Bytes &part : parts) {
      all.insert(all.end(), part.begin(), part.end());
   }
   return all;
}

// The content of the file, or nothing if it cannot be read (does not exist).
std::optional<Bytes> content(FileSystem &fileSystem, const char *path) {
   std::array<uint8_t, 8000> buffer{};
   const auto count = fileSystem.readFile(path, buffer);
   if(!count) {
      return std::nullopt;
   }
   return Bytes(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*count));
}

bool isDirectory(FileSystem &fileSystem, const char *path) {
   const auto info = fileSystem.stat(path);
   return info && info->type == EntryType::Directory;
}

template <typename T>
bool failsWith(const art::expected<T, Error> &result, Error error) {
   return !result && result.error() == error;
}

template <typename T>
bool refused(const art::expected<T, Error> &result) {
   return failsWith(result, Error::NeedsRemount);
}

using Paths = std::vector<const char *>;

struct Visitor {
   bool visit(const char *, const FileInfo &) {
      return true;
   }
};

enum class Fault : uint8_t {
   ReadTimeout,        // every read from the n-th on fails with Timeout until disarm() (like a bus that stays down)
   AnyDeviceOperation, // exactly the n-th read, program or erase fails with Io
};

// The baseline volume every step of a sweep starts from.
struct Volume {
   Volume()
      : ram(kGeometry, backing)
      , flaky(ram)
      , fileSystem(flaky) {
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.mkdir("/dir").has_value());
      REQUIRE(fileSystem.mkdir("/empty").has_value());
      REQUIRE(fileSystem.writeFile("/old", pattern(3000u, 1u)).has_value());
      REQUIRE(fileSystem.writeFile("/small", pattern(20u, 2u)).has_value());
      REQUIRE(fileSystem.writeFile("/dir/x", pattern(500u, 3u)).has_value());
      REQUIRE(fileSystem.writeFile("/existing", pattern(400u, 4u)).has_value());
      REQUIRE(fileSystem.unmount().has_value());
      baseline = backing;
   }

   // Back to the baseline volume, mounted. The read cache holds the block that mount() read last, and a call that
   // finds what it needs there does not read the device at all, so it would never see an injected failure. Reading a
   // file that lives elsewhere replaces it.
   void restore() {
      disarm();
      if(fileSystem.isMounted()) {
         static_cast<void>(fileSystem.unmount());
      }
      backing = baseline;
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(content(fileSystem, "/dir/x").has_value());
   }

   void arm(Fault fault, uint32_t position) {
      if(fault == Fault::ReadTimeout) {
         flaky.armAfter(position);
      } else {
         ram.failNthOperation(position + 1u);
      }
   }

   void disarm() {
      flaky.disarm();
      ram.clearFaults();
   }

   uint32_t operationsSeen(Fault fault) const {
      return fault == Fault::ReadTimeout ? ram.readCount() : ram.readCount() + ram.programCount() + ram.eraseCount();
   }

   std::array<uint8_t, 64u * 1024u> backing{};
   std::array<uint8_t, 64u * 1024u> baseline{};
   RamBlockDevice ram;
   FlakyReadDevice flaky;
   LittleFsFileSystem<> fileSystem;
};

// The files one step of a sweep holds open besides what the call under test opens itself.
struct Session {
   std::optional<File> pending; // the file that is being written when the call fails
   std::optional<File> second;  // a second handle, for the calls that work on a handle
   std::optional<File> reader;

   std::vector<File *> handles() {
      std::vector<File *> open;
      for(auto *file : { &pending, &second, &reader }) {
         if(file->has_value() && (*file)->isOpen()) {
            open.push_back(&**file);
         }
      }
      return open;
   }
};

// A file that is open for writing when the call under test fails. `survivesRemount` is what a remount finds when the
// mount was poisoned: what was synced, or what the file held before it was opened.
struct PendingWriter {
   const char *name;
   const char *path;
   OpenMode mode;
   Bytes before; // content of the path before open(), empty if the path did not exist
   bool syncMidway;

   Bytes written() const {
      return syncMidway ? joined({ pattern(1000u, 17u), pattern(10u, 16u) }) : pattern(1000u, 17u);
   }
   Bytes kept() const { // the part of written() that is on the medium once the writer sat through the failure
      return syncMidway ? pattern(1000u, 17u) : Bytes{};
   }
   Bytes survivesRemount() const {
      return mode == OpenMode::Append ? joined({ before, kept() }) : (syncMidway ? kept() : before);
   }
   Bytes afterClose() const { // when the mount stayed healthy: everything written, plus the tail written later
      return joined({ mode == OpenMode::Append ? before : Bytes{}, written(), pattern(10u, 19u) });
   }

   void open(FileSystem &fileSystem, Session &session) const {
      auto file = fileSystem.open(path, mode);
      REQUIRE(file.has_value());
      REQUIRE(file->write(pattern(1000u, 17u)).has_value()); // the tail stays in the file cache
      if(syncMidway) {
         REQUIRE(file->sync().has_value());
         REQUIRE(file->write(pattern(10u, 16u)).has_value());
      }
      session.pending = std::move(*file);
   }
};

const PendingWriter kWriters[] = {
   { "a new file, never synced", "/pending", OpenMode::Write, {}, false },
   { "a new file, synced and written on", "/pending", OpenMode::Write, {}, true },
   { "an existing file, truncated by open", "/existing", OpenMode::Write, pattern(400u, 4u), false },
   { "an existing file, appended to", "/existing", OpenMode::Append, pattern(400u, 4u), false },
};

// The whole state check of a poisoned mount: every public method and every file hook of the handles answers
// NeedsRemount, and the mount stays that way. Closes the handles, so it goes last.
void expectRefusesEverything(FileSystem &fileSystem, const std::vector<File *> &handles) {
   const auto data = pattern(10u);
   std::array<uint8_t, 64> buffer{};
   Visitor visitor;
   CHECK(refused(fileSystem.open("/old", OpenMode::Read)));
   CHECK(refused(fileSystem.open("/nowhere", OpenMode::Write)));
   CHECK(refused(fileSystem.open("/old", OpenMode::Append)));
   CHECK(refused(fileSystem.open("/nowhere", OpenMode::CreateNew)));
   CHECK(refused(fileSystem.remove("/small")));
   CHECK(refused(fileSystem.rename("/small", "/renamed")));
   CHECK(refused(fileSystem.mkdir("/made")));
   CHECK(refused(fileSystem.stat("/old")));
   CHECK(refused(fileSystem.visitDirectory("/", DirectoryVisitor::create<Visitor, &Visitor::visit>(visitor))));
   CHECK(refused(fileSystem.freeSpace()));
   CHECK(refused(fileSystem.readFile("/old", buffer)));
   CHECK(refused(fileSystem.writeFile("/new", data)));
   CHECK(refused(fileSystem.mount()));                 // it does not clear the state: only unmount() does
   CHECK(failsWith(fileSystem.format(), Error::Busy)); // still mounted
   CHECK(fileSystem.isMounted());
   for(File *file : handles) {
      CHECK(refused(file->read(buffer)));
      CHECK(refused(file->write(data)));
      CHECK(refused(file->seek(0u)));
      CHECK(refused(file->tell()));
      CHECK(refused(file->size()));
      CHECK(refused(file->sync()));
   }
   for(File *file : handles) {
      CHECK(refused(file->close()));
   }
}

void expectNotPoisoned(FileSystem &fileSystem) {
   CHECK(fileSystem.stat("/dir").has_value());
}

// The files of the baseline volume that a scenario does not touch are intact.
void expectBaseIntact(FileSystem &fileSystem, const Paths &except = {}) {
   struct Entry {
      const char *path;
      Bytes expected;
   };
   const Entry entries[] = { { "/old", pattern(3000u, 1u) },
                             { "/small", pattern(20u, 2u) },
                             { "/dir/x", pattern(500u, 3u) },
                             { "/existing", pattern(400u, 4u) } };
   const auto excluded = [&](const char *path) {
      return std::ranges::any_of(except, [&](const char *candidate) { return std::string(candidate) == path; });
   };
   for(const Entry &entry : entries) {
      if(!excluded(entry.path)) {
         CAPTURE(std::string(entry.path));
         CHECK(content(fileSystem, entry.path) == entry.expected);
      }
   }
   CHECK(isDirectory(fileSystem, "/dir"));
   if(!excluded("/empty")) {
      CHECK(isDirectory(fileSystem, "/empty"));
   }
}

// The first change after a mount. littlefs applies the pending move of an interrupted rename() lazily, in the first
// call that changes the volume, and until then a read can miss an entry next to the moved one (see "Poisoned mount"
// in LittleFsFileSystem.h). Every check of a volume after a remount comes after this.
void settle(FileSystem &fileSystem) {
   REQUIRE(fileSystem.writeFile("/settled", pattern(30u, 7u)).has_value());
}

// The volume takes writes again and keeps them over another remount, so nothing of the failure is left in it.
void expectUsable(FileSystem &fileSystem) {
   const auto data = pattern(3000u, 5u);
   REQUIRE(fileSystem.freeSpace().has_value());
   REQUIRE(fileSystem.writeFile("/probe", data).has_value());
   REQUIRE(fileSystem.writeFile("/dir/probe", pattern(600u, 6u)).has_value());
   REQUIRE(fileSystem.mkdir("/probe-dir").has_value());
   CHECK(content(fileSystem, "/probe") == data);
   REQUIRE(fileSystem.unmount().has_value());
   REQUIRE(fileSystem.mount().has_value());
   CHECK(content(fileSystem, "/probe") == data);
   CHECK(content(fileSystem, "/dir/probe") == pattern(600u, 6u));
   CHECK(isDirectory(fileSystem, "/probe-dir"));
}

// `path` is absent, or holds one of `allowed`.
void expectOneOf(FileSystem &fileSystem, const char *path, std::initializer_list<Bytes> allowed, bool mayBeAbsent) {
   CAPTURE(std::string(path));
   const auto found = content(fileSystem, path);
   if(!found) {
      CHECK(mayBeAbsent);
      return;
   }
   CHECK(std::ranges::any_of(allowed, [&](const Bytes &candidate) { return candidate == *found; }));
}

// Runs `attempt` once on a healthy device to count its device operations, then again with the fault at every one of
// them. `setup` opens what the step needs, before the fault is armed. `verify` looks at the step afterwards, on the
// mount that saw the failure; `attempt` returned true if it passed completely. At least `least` steps must fail,
// so that a sweep that shrinks (a call that reads less, a fixture that no longer reaches the device) is noticed.
template <typename Setup, typename Attempt, typename Verify>
uint32_t sweep(Volume &volume, Fault fault, Setup &&setup, Attempt &&attempt, Verify &&verify, uint32_t least = 1u) {
   uint32_t positions = 0u;
   {
      volume.restore();
      Session session;
      setup(volume.fileSystem, session);
      const uint32_t before = volume.operationsSeen(fault);
      REQUIRE(attempt(volume.fileSystem, session));
      positions = volume.operationsSeen(fault) - before;
   }
   REQUIRE(positions > 0u);

   uint32_t failedSteps = 0u;
   for(uint32_t position = 0u; position < positions; ++position) {
      CAPTURE(position);
      volume.restore();
      Session session;
      setup(volume.fileSystem, session);
      volume.arm(fault, position);
      const bool completed = attempt(volume.fileSystem, session);
      volume.disarm();
      failedSteps += completed ? 0u : 1u;
      verify(volume, session, completed);
   }
   CHECK(failedSteps >= least);
   return failedSteps;
}

// ---- what a call that fails leaves behind ----

struct Scenario {
   const char *name;
   // Opens the handles the call needs. Runs with the pending writer open and no fault armed.
   std::function<void(FileSystem &, Session &)> setup;
   // The call the fault hits. True if it passed completely.
   std::function<bool(FileSystem &, Session &)> attempt;
   // What the call may have left on the medium: it took effect completely or not at all. Runs after a remount.
   std::function<void(FileSystem &)> atomic;
   // The baseline files the call changes.
   Paths touches;
   // The fewest failing positions of any run of the sweep (about 90% of what was measured on littlefs v2.11.3).
   uint32_t minFailed;
};

const Scenario kScenarios[] = {
   { "writeFile of a new file",
     [](FileSystem &, Session &) {},
     [](FileSystem &fs, Session &) { return fs.writeFile("/new", pattern(3000u, 18u)).has_value(); },
     [](FileSystem &fs) { expectOneOf(fs, "/new", { Bytes{}, pattern(3000u, 18u) }, true); },
     {},
     17u },
   { "open for write that creates the file",
     [](FileSystem &, Session &) {},
     [](FileSystem &fs, Session &) {
        auto file = fs.open("/created", OpenMode::Write);
        return file.has_value() && file->close().has_value();
     },
     [](FileSystem &fs) { expectOneOf(fs, "/created", { Bytes{} }, true); },
     {},
     4u },
   { "open for create-new",
     [](FileSystem &, Session &) {},
     [](FileSystem &fs, Session &) {
        auto file = fs.open("/created", OpenMode::CreateNew);
        return file.has_value() && file->close().has_value();
     },
     [](FileSystem &fs) { expectOneOf(fs, "/created", { Bytes{} }, true); },
     {},
     4u },
   { "open for append of an existing file", // 3000 bytes: the seek to the end reads the blocks of the file
     [](FileSystem &, Session &) {},
     [](FileSystem &fs, Session &) {
        auto file = fs.open("/old", OpenMode::Append);
        return file.has_value() && file->close().has_value();
     },
     [](FileSystem &fs) { expectOneOf(fs, "/old", { pattern(3000u, 1u) }, false); },
     { "/old" },
     4u },
   { "File::write on a second writer",
     [](FileSystem &fs, Session &session) {
        auto file = fs.open("/second", OpenMode::Write);
        REQUIRE(file.has_value());
        session.second = std::move(*file);
     },
     [](FileSystem &, Session &session) { return session.second->write(pattern(3000u, 18u)).has_value(); },
     [](FileSystem &fs) { expectOneOf(fs, "/second", { Bytes{} }, false); },
     {},
     9u },
   { "File::sync on a second writer",
     [](FileSystem &fs, Session &session) {
        auto file = fs.open("/second", OpenMode::Write);
        REQUIRE(file.has_value());
        REQUIRE(file->write(pattern(700u, 18u)).has_value());
        session.second = std::move(*file);
     },
     [](FileSystem &, Session &session) { return session.second->sync().has_value(); },
     [](FileSystem &fs) { expectOneOf(fs, "/second", { Bytes{}, pattern(700u, 18u) }, false); },
     {},
     2u },
   { "File::close on a second writer",
     [](FileSystem &fs, Session &session) {
        auto file = fs.open("/second", OpenMode::Write);
        REQUIRE(file.has_value());
        REQUIRE(file->write(pattern(700u, 18u)).has_value());
        session.second = std::move(*file);
     },
     [](FileSystem &, Session &session) { return session.second->close().has_value(); },
     [](FileSystem &fs) { expectOneOf(fs, "/second", { Bytes{}, pattern(700u, 18u) }, false); },
     {},
     2u },
   { "rename within a directory",
     [](FileSystem &, Session &) {},
     [](FileSystem &fs, Session &) { return fs.rename("/small", "/renamed").has_value(); },
     [](FileSystem &fs) {
        const bool atOld = content(fs, "/small").has_value();
        const bool atNew = content(fs, "/renamed").has_value();
        CHECK(atOld != atNew);
        expectOneOf(fs, atNew ? "/renamed" : "/small", { pattern(20u, 2u) }, false);
     },
     { "/small" },
     118u },
   { "rename into another directory",
     [](FileSystem &, Session &) {},
     [](FileSystem &fs, Session &) { return fs.rename("/old", "/dir/moved").has_value(); },
     [](FileSystem &fs) {
        const bool atOld = content(fs, "/old").has_value();
        const bool atNew = content(fs, "/dir/moved").has_value();
        CHECK(atOld != atNew);
        expectOneOf(fs, atNew ? "/dir/moved" : "/old", { pattern(3000u, 1u) }, false);
     },
     { "/old" },
     161u },
   { "remove",
     [](FileSystem &, Session &) {},
     [](FileSystem &fs, Session &) { return fs.remove("/small").has_value(); },
     [](FileSystem &fs) { expectOneOf(fs, "/small", { pattern(20u, 2u) }, true); },
     { "/small" },
     4u },
   { "mkdir",
     [](FileSystem &, Session &) {},
     [](FileSystem &fs, Session &) { return fs.mkdir("/made").has_value(); },
     [](FileSystem &fs) { CHECK((!fs.stat("/made").has_value() || isDirectory(fs, "/made"))); },
     {},
     8u },
};

// A directory that has grown over several metadata pairs, on the geometry of the firmware (4 KiB blocks). littlefs
// decides whether a rename commits twice by the pair the entries are in, not by the directory they belong to.
// clang-format off
constexpr Geometry kLargeGeometry{
   .readSize = 16u, .programSize = 16u, .eraseSize = 4096u, .size = 512u * 1024u, .eraseBeforeProgram = true
};
// clang-format on
constexpr uint32_t kSplitEntries = 80u;

std::string splitPath(uint32_t index) {
   std::string path = "/d/f000";
   path[4] = static_cast<char>('0' + index / 100u);
   path[5] = static_cast<char>('0' + index / 10u % 10u);
   path[6] = static_cast<char>('0' + index % 10u);
   return path;
}

struct SplitVolume {
   SplitVolume()
      : ram(kLargeGeometry, backing)
      , flaky(ram)
      , fileSystem(flaky) {
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(fileSystem.mkdir("/d").has_value());
      for(uint32_t index = 0u; index < kSplitEntries; ++index) {
         REQUIRE(fileSystem.writeFile(splitPath(index).c_str(), pattern(40u, static_cast<uint8_t>(index))).has_value());
      }
      REQUIRE(fileSystem.writeFile("/root-file", pattern(100u, 99u)).has_value());
      REQUIRE(fileSystem.unmount().has_value());
      baseline = backing;
   }

   void restore() {
      flaky.disarm();
      if(fileSystem.isMounted()) {
         static_cast<void>(fileSystem.unmount());
      }
      backing = baseline;
      REQUIRE(fileSystem.mount().has_value());
      REQUIRE(content(fileSystem, splitPath(0u).c_str()).has_value());
   }

   std::array<uint8_t, 512u * 1024u> backing{};
   std::array<uint8_t, 512u * 1024u> baseline{};
   RamBlockDevice ram;
   FlakyReadDevice flaky;
   LittleFsFileSystem<2u, 256u, 128u> fileSystem;
};

const char *describe(Fault fault) {
   return fault == Fault::ReadTimeout ? "reads time out" : "one device operation fails";
}

} // namespace

TEST_SUITE("LittleFsFileSystem poisoned mount") {

   TEST_CASE("a failed change while another file is open for writing poisons the mount, and a remount recovers") {
      for(const Fault fault : { Fault::ReadTimeout, Fault::AnyDeviceOperation }) {
         for(const PendingWriter &writer : kWriters) {
            for(const Scenario &scenario : kScenarios) {
               CAPTURE(std::string(describe(fault)));
               CAPTURE(std::string(writer.name));
               CAPTURE(std::string(scenario.name));
               Volume volume;
               FileSystem &fs = volume.fileSystem;
               sweep(
               volume, fault,
               [&](FileSystem &system, Session &session) {
                  writer.open(system, session);
                  scenario.setup(system, session);
               },
               [&](FileSystem &system, Session &session) { return scenario.attempt(system, session); },
               [&](Volume &, Session &session, bool completed) {
                  if(completed) {
                     expectNotPoisoned(fs);
                     return;
                  }
                  expectRefusesEverything(fs, session.handles());
                  REQUIRE(fs.unmount().has_value());
                  CHECK_FALSE(fs.isMounted());
                  REQUIRE(fs.mount().has_value());
                  expectNotPoisoned(fs);
                  // The writer is dropped without a commit: it left what it had synced, or what the file held.
                  settle(fs);
                  CHECK(content(fs, writer.path) == writer.survivesRemount());
                  scenario.atomic(fs);
                  Paths touched = scenario.touches; // the writer's own file was checked above
                  touched.push_back(writer.path);
                  expectBaseIntact(fs, touched);
                  expectUsable(fs);
               },
               scenario.minFailed);
            }
         }
      }
   }

   TEST_CASE("the reproduction of issue 15 is refused instead of losing the data of the writer") {
      // The fixture of issue 15 (note 5418): read #4 of a writeFile() loses the data of the open writer, and close()
      // reported success. Everything after the failed call is refused now, close() included.
      Volume volume;
      FileSystem &fs = volume.fileSystem;
      volume.restore();
      auto writer = fs.open("/pending", OpenMode::Write);
      REQUIRE(writer.has_value());
      REQUIRE(writer->write(pattern(1000u, 17u)).has_value());
      volume.arm(Fault::ReadTimeout, 4u);
      REQUIRE_FALSE(fs.writeFile("/new", pattern(3000u, 18u)).has_value());
      volume.disarm();
      CHECK(refused(fs.writeFile("/new", pattern(3000u, 18u)))); // the retry
      CHECK(refused(fs.writeFile("/other", pattern(2500u, 9u))));
      CHECK(refused(fs.mkdir("/dir/sub")));
      CHECK(refused(writer->write(pattern(10u, 19u))));
      CHECK(refused(writer->close())); // used to report success
      REQUIRE(fs.unmount().has_value());
      REQUIRE(fs.mount().has_value());
      CHECK(content(fs, "/pending") == Bytes{}); // nothing of it was ever synced, and the caller knows
      expectUsable(fs);
   }

   TEST_CASE("a failed rename poisons the mount at every position, and a remount recovers") {
      struct Move {
         const char *from; // as the caller spells it
         const char *to;
         const char *source; // the same paths, canonical
         const char *target;
         Bytes expected;
         bool directory;
         uint32_t minFailed; // about 90% of the fewest failing positions measured on littlefs v2.11.3
      };
      // A file into a subdirectory, a file out of a subdirectory (the shape that trips a littlefs assertion when it is
      // retried), a directory into a subdirectory, renames within one directory (whether one commits twice depends on
      // the metadata pair the entries are in, see the split directory test), and other spellings of these.
      const Move moves[] = {
         { "/old", "/dir/moved", "/old", "/dir/moved", pattern(3000u, 1u), false, 161u },
         { "/dir/x", "/xx", "/dir/x", "/xx", pattern(500u, 3u), false, 66u },
         { "/empty", "/dir/empty", "/empty", "/dir/empty", {}, true, 161u },
         { "/small", "/renamed", "/small", "/renamed", pattern(20u, 2u), false, 118u },
         { "/old", "/small", "/old", "/small", pattern(3000u, 1u), false, 118u },
         { "/dir/x", "/dir/y", "/dir/x", "/dir/y", pattern(500u, 3u), false, 33u },
         { "/./dir/x", "//xx", "/dir/x", "/xx", pattern(500u, 3u), false, 66u },
         { "/./dir//x", "/dir/./y", "/dir/x", "/dir/y", pattern(500u, 3u), false, 33u },
      };
      for(const Fault fault : { Fault::ReadTimeout, Fault::AnyDeviceOperation }) {
         for(const Move &move : moves) {
            CAPTURE(std::string(describe(fault)));
            CAPTURE(std::string(move.from));
            CAPTURE(std::string(move.to));
            Volume volume;
            FileSystem &fs = volume.fileSystem;
            sweep(
            volume, fault,
            [&](FileSystem &system, Session &session) {
               // A reader is open: it is no writer, so only the rename decides.
               auto reader = system.open("/existing", OpenMode::Read);
               REQUIRE(reader.has_value());
               session.reader = std::move(*reader);
            },
            [&](FileSystem &system, Session &) { return system.rename(move.from, move.to).has_value(); },
            [&](Volume &, Session &session, bool completed) {
               if(completed) {
                  expectNotPoisoned(fs);
                  return;
               }
               expectRefusesEverything(fs, session.handles());
               REQUIRE(fs.unmount().has_value());
               REQUIRE(fs.mount().has_value());
               expectNotPoisoned(fs);
               settle(fs);
               // The move took place or did not, never both and never neither. A target that is a baseline file
               // (the rename replaces it) is either replaced or as it was.
               const bool moved = !fs.stat(move.source).has_value();
               const bool targetIsBaseline = std::string(move.target) == "/small";
               if(!move.directory) {
                  CHECK(content(fs, moved ? move.target : move.source) == move.expected);
               } else {
                  CHECK(isDirectory(fs, moved ? move.target : move.source));
               }
               if(!moved && !targetIsBaseline) {
                  CHECK_FALSE(fs.stat(move.target).has_value());
               }
               if(!moved && targetIsBaseline) {
                  CHECK(content(fs, move.target) == pattern(20u, 2u));
               }
               expectBaseIntact(fs, { move.source, move.target });
               expectUsable(fs);
            },
            move.minFailed);
         }
      }
   }

   TEST_CASE("a failure with no other writer open does not poison the mount") {
      // The same calls as above, with nothing open for writing but the file they work on: littlefs is left in a state
      // that a retry handles (TestLittleFsProgramCache.cpp), so the mount keeps working.
      struct Case {
         const char *name;
         std::function<void(FileSystem &, Session &)> setup;
         std::function<bool(FileSystem &, Session &)> attempt;
      };
      const auto openWriter = [](FileSystem &fs, Session &session) {
         auto file = fs.open("/second", OpenMode::Write);
         REQUIRE(file.has_value());
         REQUIRE(file->write(pattern(700u, 18u)).has_value());
         session.second = std::move(*file);
      };
      const Case cases[] = {
         { "writeFile", [](FileSystem &, Session &) {},
           [](FileSystem &fs, Session &) { return fs.writeFile("/new", pattern(3000u, 18u)).has_value(); } },
         { "File::write of the only writer", openWriter,
           [](FileSystem &, Session &session) { return session.second->write(pattern(3000u, 18u)).has_value(); } },
         { "File::sync of the only writer", openWriter,
           [](FileSystem &, Session &session) { return session.second->sync().has_value(); } },
         { "File::close of the only writer", openWriter,
           [](FileSystem &, Session &session) { return session.second->close().has_value(); } },
         { "remove", [](FileSystem &, Session &) {},
           [](FileSystem &fs, Session &) { return fs.remove("/small").has_value(); } },
         { "mkdir", [](FileSystem &, Session &) {},
           [](FileSystem &fs, Session &) { return fs.mkdir("/made").has_value(); } },
         { "with a reader open",
           [](FileSystem &fs, Session &session) {
              auto file = fs.open("/existing", OpenMode::Read);
              REQUIRE(file.has_value());
              session.reader = std::move(*file);
           },
           [](FileSystem &fs, Session &) { return fs.writeFile("/new", pattern(3000u, 18u)).has_value(); } },
      };
      for(const Fault fault : { Fault::ReadTimeout, Fault::AnyDeviceOperation }) {
         for(const Case &item : cases) {
            CAPTURE(std::string(describe(fault)));
            CAPTURE(std::string(item.name));
            Volume volume;
            FileSystem &fs = volume.fileSystem;
            sweep(volume, fault, item.setup, item.attempt, [&](Volume &, Session &, bool) {
               // Not refused, and it takes the next writes as a healthy mount would.
               expectNotPoisoned(fs);
               CHECK_FALSE(refused(fs.mkdir("/after")));
               CHECK_FALSE(refused(fs.writeFile("/after-too", pattern(300u, 20u))));
            });
         }
      }
   }

   TEST_CASE("a read that fails while a file is open for writing does not poison the mount or cost it data") {
      struct Case {
         const char *name;
         std::function<void(FileSystem &, Session &)> setup;
         std::function<bool(FileSystem &, Session &)> attempt;
      };
      const auto openReader = [](FileSystem &fs, Session &session) {
         auto file = fs.open("/old", OpenMode::Read);
         REQUIRE(file.has_value());
         session.reader = std::move(*file);
      };
      const Case cases[] = {
         { "stat", [](FileSystem &, Session &) {},
           [](FileSystem &fs, Session &) { return fs.stat("/dir/x").has_value(); } },
         { "freeSpace", [](FileSystem &, Session &) {},
           [](FileSystem &fs, Session &) { return fs.freeSpace().has_value(); } },
         { "visitDirectory", [](FileSystem &, Session &) {},
           [](FileSystem &fs, Session &) {
              Visitor visitor;
              return fs.visitDirectory("/", DirectoryVisitor::create<Visitor, &Visitor::visit>(visitor)).has_value();
           } },
         { "readFile", [](FileSystem &, Session &) {},
           [](FileSystem &fs, Session &) { return content(fs, "/old").has_value(); } },
         { "open for read", [](FileSystem &, Session &) {},
           [](FileSystem &fs, Session &) { return fs.open("/dir/x", OpenMode::Read).has_value(); } },
         { "File::read", openReader,
           [](FileSystem &, Session &session) {
              std::array<uint8_t, 2000> buffer{};
              return session.reader->read(buffer).has_value();
           } },
         { "File::seek on a second writer, which flushes it",
           [](FileSystem &fs, Session &session) {
              auto file = fs.open("/second", OpenMode::Write);
              REQUIRE(file.has_value());
              REQUIRE(file->write(pattern(700u, 18u)).has_value());
              session.second = std::move(*file);
           },
           [](FileSystem &, Session &session) { return session.second->seek(0u).has_value(); } },
         { "File::seek and read", openReader,
           [](FileSystem &, Session &session) {
              std::array<uint8_t, 100> buffer{};
              return session.reader->seek(2500u).has_value() && session.reader->read(buffer).has_value();
           } },
      };
      for(const Fault fault : { Fault::ReadTimeout, Fault::AnyDeviceOperation }) {
         for(const PendingWriter &writer : kWriters) {
            for(const Case &item : cases) {
               CAPTURE(std::string(describe(fault)));
               CAPTURE(std::string(writer.name));
               CAPTURE(std::string(item.name));
               Volume volume;
               FileSystem &fs = volume.fileSystem;
               sweep(
               volume, fault,
               [&](FileSystem &system, Session &session) {
                  writer.open(system, session);
                  item.setup(system, session);
               },
               item.attempt,
               [&](Volume &, Session &session, bool) {
                  expectNotPoisoned(fs);
                  // The writer goes on and its data, unsynced and synced, is all there after close().
                  REQUIRE(session.pending->write(pattern(10u, 19u)).has_value());
                  REQUIRE(session.pending->close().has_value());
                  CHECK(content(fs, writer.path) == writer.afterClose());
                  REQUIRE(fs.unmount().has_value());
                  REQUIRE(fs.mount().has_value());
                  CHECK(content(fs, writer.path) == writer.afterClose());
               });
            }
         }
      }
   }

   TEST_CASE("a call that littlefs refuses before it changes anything does not poison the mount") {
      // Each of these fails on a healthy device with an error that says nothing was touched. With a writer open they
      // must keep working like any error of a healthy volume, and the writer must not lose anything.
      struct Refusal {
         const char *name;
         std::function<std::optional<Error>(FileSystem &)> call; // the error the call fails with
         Error expected;
      };
      const auto errorOf = [](const auto &result) -> std::optional<Error> {
         return result ? std::nullopt : std::optional<Error>(result.error());
      };
      // clang-format off
      const Refusal refusals[] = {
         { "remove of a missing file",
           [&](FileSystem &fs) { return errorOf(fs.remove("/missing")); }, Error::NoEntry },
         { "mkdir of an existing directory",
           [&](FileSystem &fs) { return errorOf(fs.mkdir("/dir")); }, Error::Exists },
         { "mkdir below a file",
           [&](FileSystem &fs) { return errorOf(fs.mkdir("/old/sub")); }, Error::NotDirectory },
         { "mkdir below a missing directory",
           [&](FileSystem &fs) { return errorOf(fs.mkdir("/missing/sub")); }, Error::NoEntry },
         { "remove of a directory that is not empty",
           [&](FileSystem &fs) { return errorOf(fs.remove("/dir")); }, Error::NotEmpty },
         { "open of a directory for writing",
           [&](FileSystem &fs) { return errorOf(fs.open("/dir", OpenMode::Write)); }, Error::IsDirectory },
         { "open for writing below a missing directory",
           [&](FileSystem &fs) { return errorOf(fs.open("/missing/f", OpenMode::Write)); }, Error::NoEntry },
         { "open for writing below a file",
           [&](FileSystem &fs) { return errorOf(fs.open("/old/f", OpenMode::Write)); }, Error::NotDirectory },
         { "create-new of an existing file",
           [&](FileSystem &fs) { return errorOf(fs.open("/small", OpenMode::CreateNew)); }, Error::Exists },
         { "rename of a missing file",
           [&](FileSystem &fs) { return errorOf(fs.rename("/missing", "/moved")); }, Error::NoEntry },
         { "rename of a missing file into another directory",
           [&](FileSystem &fs) { return errorOf(fs.rename("/missing", "/dir/moved")); }, Error::NoEntry },
         { "rename into a missing directory",
           [&](FileSystem &fs) { return errorOf(fs.rename("/small", "/missing/f")); }, Error::NoEntry },
         { "rename of a directory onto a file",
           [&](FileSystem &fs) { return errorOf(fs.rename("/empty", "/dir/x")); }, Error::NotDirectory },
         { "rename of a file onto a directory",
           [&](FileSystem &fs) { return errorOf(fs.rename("/small", "/dir")); }, Error::IsDirectory },
         { "rename onto a directory that is not empty",
           [&](FileSystem &fs) { return errorOf(fs.rename("/empty", "/dir")); }, Error::NotEmpty },
         { "rename of the root",
           [&](FileSystem &fs) { return errorOf(fs.rename("/", "/moved")); }, Error::InvalidArgument },
         { "rename onto the root",
           [&](FileSystem &fs) { return errorOf(fs.rename("/small", "/")); }, Error::InvalidArgument },
      };
      // clang-format on
      for(const PendingWriter &writer : kWriters) {
         for(const Refusal &refusal : refusals) {
            CAPTURE(std::string(writer.name));
            CAPTURE(std::string(refusal.name));
            Volume volume;
            FileSystem &fs = volume.fileSystem;
            volume.restore();
            Session session;
            writer.open(fs, session);
            const auto error = refusal.call(fs);
            REQUIRE(error.has_value());
            CHECK((*error == refusal.expected));
            expectNotPoisoned(fs);
            REQUIRE(session.pending->write(pattern(10u, 19u)).has_value());
            REQUIRE(session.pending->close().has_value());
            CHECK(content(fs, writer.path) == writer.afterClose());
            expectBaseIntact(fs, { writer.path });
            expectUsable(fs);
         }
      }
   }

   TEST_CASE("unmount of a poisoned mount does not commit, and the destructor does not either") {
      const auto poison = [](Volume &volume, Session &session) {
         volume.restore();
         kWriters[1].open(volume.fileSystem, session);
         volume.arm(Fault::ReadTimeout, 0u);
         REQUIRE_FALSE(volume.fileSystem.mkdir("/made").has_value());
         volume.disarm();
      };

      SUBCASE("unmount()") {
         Volume volume;
         Session session;
         poison(volume, session);
         volume.ram.resetCounters();
         CHECK(volume.fileSystem.unmount().has_value());
         CHECK(volume.ram.programCount() == 0u);
         CHECK(volume.ram.eraseCount() == 0u);
         // The handles are stale, in every hook, including after the next mount.
         CHECK(failsWith(session.pending->write(pattern(10u)), Error::BadHandle));
         REQUIRE(volume.fileSystem.mount().has_value());
         std::array<uint8_t, 8> buffer{};
         CHECK(failsWith(session.pending->read(buffer), Error::BadHandle));
         CHECK(failsWith(session.pending->seek(0u), Error::BadHandle));
         CHECK(failsWith(session.pending->tell(), Error::BadHandle));
         CHECK(failsWith(session.pending->size(), Error::BadHandle));
         CHECK(failsWith(session.pending->sync(), Error::BadHandle));
         CHECK(failsWith(session.pending->close(), Error::BadHandle));
      }

      SUBCASE("the control: a healthy unmount closes the writer, and that is what it writes") {
         Volume volume;
         volume.restore();
         Session session;
         kWriters[1].open(volume.fileSystem, session);
         volume.ram.resetCounters();
         CHECK(volume.fileSystem.unmount().has_value());
         CHECK(volume.ram.programCount() > 0u);
         REQUIRE(volume.fileSystem.mount().has_value());
         CHECK(content(volume.fileSystem, "/pending") == kWriters[1].written());
      }

      SUBCASE("the destructor") {
         Volume volume; // unmounted, on the baseline
         {
            LittleFsFileSystem<> local(volume.flaky);
            REQUIRE(local.mount().has_value());
            auto writer = local.open("/local", OpenMode::Write);
            REQUIRE(writer.has_value());
            REQUIRE(writer->write(pattern(1000u, 17u)).has_value());
            auto second = local.open("/second", OpenMode::Write);
            REQUIRE(second.has_value());
            volume.flaky.armAfter(0u);
            REQUIRE_FALSE(local.mkdir("/made").has_value());
            volume.flaky.disarm();
            volume.ram.resetCounters();
         } // the files are destroyed first, then the file system
         CHECK(volume.ram.programCount() == 0u);
         CHECK(volume.ram.eraseCount() == 0u);
      }
   }

   TEST_CASE("unmount and mount clear the state, and the next failure poisons again") {
      Volume volume;
      FileSystem &fs = volume.fileSystem;
      for(int round = 0; round < 3; ++round) {
         CAPTURE(round);
         volume.restore();
         Session session;
         kWriters[0].open(fs, session);
         expectNotPoisoned(fs);
         volume.arm(Fault::AnyDeviceOperation, 0u);
         REQUIRE_FALSE(fs.mkdir("/made").has_value());
         volume.disarm();
         CHECK(refused(fs.stat("/old")));
         CHECK(fs.unmount().has_value()); // twice is fine
         CHECK(fs.unmount().has_value());
         CHECK_FALSE(fs.isMounted());
         // Not mounted is NotMounted again, not NeedsRemount.
         CHECK(failsWith(fs.stat("/old"), Error::NotMounted));
         REQUIRE(fs.mount().has_value());
         expectNotPoisoned(fs);
      }
   }

   TEST_CASE("a failed rename inside a directory that spans several metadata pairs poisons the mount") {
      // The two-commit path of lfs_rename() is taken when the old and the new entry are in different metadata pairs.
      // Both names have the same parent here, and it is a rename of the root-level kind that a path comparison would
      // have exempted: without the poison, 2 of 820 failing positions lost unrelated files when the caller went on.
      struct Rename {
         const char *from;
         const char *to;
         uint32_t minFailed; // about 90% of the fewest failing positions measured on littlefs v2.11.3
      };
      const Rename renames[] = { { "/d/f000", "/d/zz", 730u }, { "/d/f079", "/d/aa", 650u } };
      for(const Rename &rename : renames) {
         CAPTURE(std::string(rename.from));
         const auto volume = std::make_unique<SplitVolume>();
         FileSystem &fs = volume->fileSystem;
         uint32_t positions = 0u;
         {
            volume->restore();
            const uint32_t before = volume->ram.readCount();
            REQUIRE(fs.rename(rename.from, rename.to).has_value());
            positions = volume->ram.readCount() - before;
         }
         CHECK(positions > 500u); // a rename in a small directory reads about 70 times: this one commits twice

         uint32_t failedSteps = 0u;
         for(uint32_t position = 0u; position < positions; ++position) {
            CAPTURE(position);
            volume->restore();
            volume->flaky.armAfter(position);
            const bool completed = fs.rename(rename.from, rename.to).has_value();
            volume->flaky.disarm();
            if(completed) {
               continue;
            }
            ++failedSteps;
            CHECK(refused(fs.stat("/root-file")));
            REQUIRE(fs.unmount().has_value());
            REQUIRE(fs.mount().has_value());
            REQUIRE(fs.writeFile("/settled", pattern(30u, 7u)).has_value());
            // Everything but the renamed entry is there, and the renamed entry is in exactly one place.
            for(uint32_t index = 0u; index < kSplitEntries; ++index) {
               const std::string path = splitPath(index);
               if(path != rename.from) {
                  CHECK(content(fs, path.c_str()) == pattern(40u, static_cast<uint8_t>(index)));
               }
            }
            CHECK(content(fs, "/root-file") == pattern(100u, 99u));
            const uint32_t renamed = std::string(rename.from) == "/d/f000" ? 0u : 79u;
            const bool moved = !content(fs, rename.from).has_value();
            CHECK(content(fs, moved ? rename.to : rename.from) == pattern(40u, static_cast<uint8_t>(renamed)));
            CHECK((moved || !content(fs, rename.to).has_value()));
         }
         CHECK(failedSteps >= rename.minFailed);
      }
   }

   TEST_CASE("a close that fails during unmount drops the writers behind it without a commit") {
      // unmount() closes the writers in turn. A close that fails poisons the mount (the other writer is still open),
      // and the writers after it are then dropped like on any poisoned mount: unmount() reports the error, and what
      // they had not synced is lost.
      // One failing operation, so that a close through the poisoned state would succeed and be noticed.
      Volume volume;
      FileSystem &fs = volume.fileSystem;
      uint32_t failedUnmounts = 0u;
      sweep(
      volume, Fault::AnyDeviceOperation,
      [&](FileSystem &system, Session &session) {
         kWriters[0].open(system, session);
         auto second = system.open("/second", OpenMode::Write);
         REQUIRE(second.has_value());
         REQUIRE(second->write(pattern(700u, 18u)).has_value());
         session.second = std::move(*second);
      },
      [&](FileSystem &system, Session &) { return system.unmount().has_value(); },
      [&](Volume &, Session &, bool completed) {
         CHECK_FALSE(fs.isMounted());
         REQUIRE(fs.mount().has_value());
         REQUIRE(fs.writeFile("/settled", pattern(30u, 7u)).has_value());
         if(completed) {
            CHECK(content(fs, "/pending") == kWriters[0].written());
            CHECK(content(fs, "/second") == pattern(700u, 18u));
            return;
         }
         ++failedUnmounts;
         // The first writer's close failed before it committed, or later: then it has all of its data or none. In the
         // first case the mount was poisoned with the second writer still open, and that one was never closed through
         // the poisoned state. In the second case the failing close was the last one, and it cannot be told from here.
         expectOneOf(fs, "/pending", { Bytes{}, kWriters[0].written() }, false);
         expectOneOf(fs, "/second", { Bytes{}, pattern(700u, 18u) }, false);
         if(content(fs, "/pending") == Bytes{}) {
            CHECK(content(fs, "/second") == Bytes{});
         }
         expectUsable(fs);
      });
      CHECK(failedUnmounts >= 9u);
   }
}
