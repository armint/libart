#include "InMemoryFileSystem.h"
#include "storage/File.h"
#include "storage/FileSystem.h"
#include <array>
#include <doctest/doctest.h>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace art::storage;
using namespace art::storage::test;

namespace {

// Compares through a bool: doctest would stringify Error through the toString() found by ADL, which does not fit.
template <typename T>
bool failedWith(const std::expected<T, Error> &result, Error error) {
   return !result.has_value() && result.error() == error;
}

std::vector<uint8_t> bytes(std::string_view text) {
   return { text.begin(), text.end() };
}

// Returns a File by value, as a caller of open() does with a helper function.
File openForWriting(FileSystem &fileSystem, const char *path) {
   auto file = fileSystem.open(path, OpenMode::Write);
   REQUIRE(file.has_value());
   return std::move(*file);
}

struct MountedFileSystem {
   MountedFileSystem() {
      REQUIRE(fileSystem.mount().has_value());
   }

   InMemoryFileSystem fileSystem;
};

} // namespace

TEST_SUITE("art::storage::File") {

   TEST_CASE("File is move-only and two words wide") {
      static_assert(!std::is_copy_constructible_v<File>);
      static_assert(!std::is_copy_assignable_v<File>);
      static_assert(std::is_nothrow_move_constructible_v<File>);
      static_assert(std::is_nothrow_move_assignable_v<File>);
      static_assert(!std::is_polymorphic_v<File>);
      static_assert(sizeof(File) <= 2u * sizeof(void *));
   }

   TEST_CASE("a default-constructed File is inert") {
      File file;
      std::array<uint8_t, 4> buffer{};
      CHECK_FALSE(file.isOpen());
      CHECK(failedWith(file.read(buffer), Error::BadHandle));
      CHECK(failedWith(file.write(buffer), Error::BadHandle));
      CHECK(failedWith(file.seek(0u), Error::BadHandle));
      CHECK(failedWith(file.tell(), Error::BadHandle));
      CHECK(failedWith(file.size(), Error::BadHandle));
      CHECK(failedWith(file.sync(), Error::BadHandle));
      CHECK(failedWith(file.close(), Error::BadHandle));
   }

   TEST_CASE("the destructor closes the file") {
      MountedFileSystem fixture;
      {
         auto file = fixture.fileSystem.open("/a", OpenMode::Write);
         REQUIRE(file.has_value());
         CHECK(file->isOpen());
         CHECK(fixture.fileSystem.openFileCount() == 1u);
      }
      CHECK(fixture.fileSystem.openFileCount() == 0u);
      CHECK(fixture.fileSystem.closeCalls() == 1u);
   }

   TEST_CASE("a File can be moved out of a function and stays open") {
      MountedFileSystem fixture;
      {
         File file = openForWriting(fixture.fileSystem, "/a");
         CHECK(file.isOpen());
         CHECK(fixture.fileSystem.openFileCount() == 1u);
         CHECK(file.write(bytes("abc")).has_value());
      }
      CHECK(fixture.fileSystem.openFileCount() == 0u);
      CHECK(fixture.fileSystem.closeCalls() == 1u);
      CHECK(fixture.fileSystem.content("/a") == bytes("abc"));
   }

   TEST_CASE("move construction transfers ownership and leaves the source inert") {
      MountedFileSystem fixture;
      {
         File source = openForWriting(fixture.fileSystem, "/a");
         File target{ std::move(source) };
         CHECK_FALSE(source.isOpen());                                  // NOLINT(bugprone-use-after-move)
         CHECK(failedWith(source.write(bytes("x")), Error::BadHandle)); // NOLINT(bugprone-use-after-move)
         CHECK(target.write(bytes("abc")).has_value());
         CHECK(fixture.fileSystem.openFileCount() == 1u);
      }
      CHECK(fixture.fileSystem.closeCalls() == 1u);
   }

   TEST_CASE("move assignment closes the file it overwrites") {
      MountedFileSystem fixture;
      File first = openForWriting(fixture.fileSystem, "/first");
      File second = openForWriting(fixture.fileSystem, "/second");
      REQUIRE(fixture.fileSystem.openFileCount() == 2u);
      first = std::move(second);
      CHECK(fixture.fileSystem.openFileCount() == 1u);
      CHECK(fixture.fileSystem.closeCalls() == 1u);
      CHECK_FALSE(second.isOpen()); // NOLINT(bugprone-use-after-move)
      CHECK(first.write(bytes("abc")).has_value());
      CHECK(first.close().has_value());
      CHECK(fixture.fileSystem.content("/second") == bytes("abc"));
   }

   TEST_CASE("self move assignment keeps the file open") {
      MountedFileSystem fixture;
      File file = openForWriting(fixture.fileSystem, "/a");
      File &alias = file;
      file = std::move(alias);
      CHECK(file.isOpen());
      CHECK(fixture.fileSystem.closeCalls() == 0u);
   }

   TEST_CASE("explicit close reports the error of the final flush and does not close twice") {
      MountedFileSystem fixture;
      {
         File file = openForWriting(fixture.fileSystem, "/a");
         fixture.fileSystem.failNextClose(Error::NoSpace);
         const auto result = file.close();
         REQUIRE_FALSE(result.has_value());
         CHECK(failedWith(result, Error::NoSpace));
         CHECK_FALSE(file.isOpen());
         CHECK(failedWith(file.close(), Error::BadHandle));
      }
      CHECK(fixture.fileSystem.closeCalls() == 1u);
      CHECK(fixture.fileSystem.openFileCount() == 0u);
   }

   TEST_CASE("the destructor swallows a close error") {
      MountedFileSystem fixture;
      {
         File file = openForWriting(fixture.fileSystem, "/a");
         fixture.fileSystem.failNextClose(Error::Io);
      }
      CHECK(fixture.fileSystem.openFileCount() == 0u);
      CHECK(fixture.fileSystem.closeCalls() == 1u);
   }

   TEST_CASE("sync reports the error and keeps the file open") {
      MountedFileSystem fixture;
      File file = openForWriting(fixture.fileSystem, "/a");
      fixture.fileSystem.failNextSync(Error::Io);
      CHECK(failedWith(file.sync(), Error::Io));
      CHECK(file.isOpen());
      CHECK(file.sync().has_value());
   }

   TEST_CASE("a handle that unmount() invalidated reports BadHandle") {
      MountedFileSystem fixture;
      File file = openForWriting(fixture.fileSystem, "/a");
      REQUIRE(fixture.fileSystem.unmount().has_value());
      std::array<uint8_t, 2> buffer{};
      CHECK(failedWith(file.read(buffer), Error::BadHandle));
      CHECK(failedWith(file.write(buffer), Error::BadHandle));
      CHECK(failedWith(file.seek(0u), Error::BadHandle));
      CHECK(failedWith(file.tell(), Error::BadHandle));
      CHECK(failedWith(file.size(), Error::BadHandle));
      CHECK(failedWith(file.sync(), Error::BadHandle));
      CHECK(failedWith(file.close(), Error::BadHandle));
   }

   TEST_CASE("a stale handle stays stale when its pool slot is reused") {
      MountedFileSystem fixture;
      File stale = openForWriting(fixture.fileSystem, "/a");
      REQUIRE(fixture.fileSystem.unmount().has_value());
      REQUIRE(fixture.fileSystem.mount().has_value());
      // The new file takes the slot the stale handle pointed to.
      File fresh = openForWriting(fixture.fileSystem, "/b");
      CHECK(failedWith(stale.write(bytes("x")), Error::BadHandle));
      CHECK(fresh.write(bytes("y")).has_value());
      // The stale File must not close the file that reuses its slot when it is destroyed.
      CHECK(failedWith(stale.close(), Error::BadHandle));
      CHECK(fixture.fileSystem.openFileCount() == 1u);
   }

   TEST_CASE("read returns fewer bytes at the end of the file and 0 beyond it") {
      MountedFileSystem fixture;
      REQUIRE(fixture.fileSystem.writeFile("/a", bytes("abcdef")).has_value());
      auto file = fixture.fileSystem.open("/a", OpenMode::Read);
      REQUIRE(file.has_value());
      std::array<uint8_t, 4> buffer{};
      CHECK(file->read(buffer).value() == 4u);
      CHECK(file->read(buffer).value() == 2u);
      CHECK(file->read(buffer).value() == 0u);
      CHECK(file->tell().value() == 6u);
      CHECK(file->seek(100u).has_value());
      CHECK(file->read(buffer).value() == 0u);
   }

   TEST_CASE("write, seek, tell and size write into the middle of a file") {
      MountedFileSystem fixture;
      REQUIRE(fixture.fileSystem.writeFile("/image", bytes("aaaaaaaa")).has_value());
      auto file = fixture.fileSystem.open("/image", OpenMode::ReadWrite);
      REQUIRE(file.has_value());
      CHECK(file->tell().value() == 0u);
      CHECK(file->seek(3u).has_value());
      CHECK(file->write(bytes("XY")).has_value());
      CHECK(file->tell().value() == 5u);
      CHECK(file->size().value() == 8u);
      CHECK(file->seek(0u).has_value());
      std::array<uint8_t, 8> buffer{};
      CHECK(file->read(buffer).value() == 8u);
      CHECK(std::string{ buffer.begin(), buffer.end() } == "aaaXYaaa");
   }

   TEST_CASE("open modes") {
      MountedFileSystem fixture;
      CHECK(failedWith(fixture.fileSystem.open("/missing", OpenMode::Read), Error::NoEntry));

      REQUIRE(fixture.fileSystem.writeFile("/a", bytes("abc")).has_value());
      SUBCASE("Write truncates") {
         {
            auto file = fixture.fileSystem.open("/a", OpenMode::Write);
         }
         CHECK(fixture.fileSystem.content("/a").empty());
      }
      SUBCASE("Append keeps the content and writes to the end") {
         auto file = fixture.fileSystem.open("/a", OpenMode::Append);
         REQUIRE(file.has_value());
         CHECK(file->tell().value() == 3u);
         REQUIRE(file->seek(0u).has_value());
         CHECK(file->write(bytes("de")).has_value());
         CHECK(file->close().has_value());
         CHECK(fixture.fileSystem.content("/a") == bytes("abcde"));
      }
      SUBCASE("ReadWrite creates a missing file and keeps an existing one") {
         {
            auto file = fixture.fileSystem.open("/new", OpenMode::ReadWrite);
         }
         CHECK(fixture.fileSystem.content("/new").empty());
         {
            auto file = fixture.fileSystem.open("/a", OpenMode::ReadWrite);
         }
         CHECK(fixture.fileSystem.content("/a") == bytes("abc"));
      }
      SUBCASE("access is limited to the mode") {
         auto readOnly = fixture.fileSystem.open("/a", OpenMode::Read);
         REQUIRE(readOnly.has_value());
         CHECK(failedWith(readOnly->write(bytes("x")), Error::InvalidArgument));
         auto writeOnly = fixture.fileSystem.open("/a", OpenMode::Write);
         REQUIRE(writeOnly.has_value());
         std::array<uint8_t, 1> buffer{};
         CHECK(failedWith(writeOnly->read(buffer), Error::InvalidArgument));
      }
   }

   TEST_CASE("open fails when the slot pool is exhausted and recovers after a close") {
      MountedFileSystem fixture;
      std::vector<File> files;
      for(size_t i = 0; i < InMemoryFileSystem::kMaxOpenFiles; ++i) {
         files.push_back(openForWriting(fixture.fileSystem, ("/f" + std::to_string(i)).c_str()));
      }
      CHECK(failedWith(fixture.fileSystem.open("/extra", OpenMode::Write), Error::NoMemory));
      REQUIRE(files.back().close().has_value());
      CHECK(fixture.fileSystem.open("/extra", OpenMode::Write).has_value());
   }

   TEST_CASE("open on an unmounted file system fails with NotMounted") {
      InMemoryFileSystem fileSystem;
      CHECK(failedWith(fileSystem.open("/a", OpenMode::Write), Error::NotMounted));
   }
}

TEST_SUITE("art::storage::FileSystem whole-file helpers") {

   TEST_CASE("writeFile then readFile round trips through the handle API") {
      MountedFileSystem fixture;
      const auto content = bytes("hello sentinode");
      REQUIRE(fixture.fileSystem.writeFile("/a", content).has_value());
      CHECK(fixture.fileSystem.openCalls() == 1u);
      CHECK(fixture.fileSystem.openFileCount() == 0u);

      std::array<uint8_t, 32> buffer{};
      const auto bytesRead = fixture.fileSystem.readFile("/a", buffer);
      REQUIRE(bytesRead.has_value());
      CHECK(bytesRead.value() == content.size());
      CHECK(std::equal(content.begin(), content.end(), buffer.begin()));
      CHECK(fixture.fileSystem.openCalls() == 2u);
      CHECK(fixture.fileSystem.openFileCount() == 0u);
   }

   TEST_CASE("writeFile replaces the previous content") {
      MountedFileSystem fixture;
      REQUIRE(fixture.fileSystem.writeFile("/a", bytes("long old content")).has_value());
      REQUIRE(fixture.fileSystem.writeFile("/a", bytes("new")).has_value());
      CHECK(fixture.fileSystem.content("/a") == bytes("new"));
   }

   TEST_CASE("an empty file round trips") {
      MountedFileSystem fixture;
      REQUIRE(fixture.fileSystem.writeFile("/empty", {}).has_value());
      std::array<uint8_t, 4> buffer{};
      CHECK(fixture.fileSystem.readFile("/empty", buffer).value() == 0u);
      CHECK(fixture.fileSystem.readFile("/empty", {}).value() == 0u);
   }

   TEST_CASE("readFile refuses a buffer smaller than the file instead of returning a short read") {
      MountedFileSystem fixture;
      REQUIRE(fixture.fileSystem.writeFile("/a", bytes("0123456789")).has_value());
      std::array<uint8_t, 9> tooSmall{};
      tooSmall.fill(0xAAu);
      const auto result = fixture.fileSystem.readFile("/a", tooSmall);
      REQUIRE_FALSE(result.has_value());
      CHECK(failedWith(result, Error::NoMemory));
      CHECK(std::ranges::all_of(tooSmall, [](uint8_t value) { return value == 0xAAu; }));
      CHECK(fixture.fileSystem.openFileCount() == 0u);

      std::array<uint8_t, 10> exact{};
      CHECK(fixture.fileSystem.readFile("/a", exact).value() == 10u);
   }

   TEST_CASE("readFile and writeFile propagate open errors") {
      MountedFileSystem fixture;
      std::array<uint8_t, 4> buffer{};
      CHECK(failedWith(fixture.fileSystem.readFile("/missing", buffer), Error::NoEntry));
      CHECK(failedWith(fixture.fileSystem.writeFile("/no/dir/a", bytes("x")), Error::NoEntry));
      REQUIRE(fixture.fileSystem.mkdir("/dir").has_value());
      CHECK(failedWith(fixture.fileSystem.readFile("/dir", buffer), Error::IsDirectory));
      REQUIRE(fixture.fileSystem.unmount().has_value());
      CHECK(failedWith(fixture.fileSystem.readFile("/a", buffer), Error::NotMounted));
      CHECK(failedWith(fixture.fileSystem.writeFile("/a", bytes("x")), Error::NotMounted));
   }

   TEST_CASE("writeFile reports a failed write and a failed final flush") {
      MountedFileSystem fixture;
      SUBCASE("write error") {
         fixture.fileSystem.failNextWrite(Error::NoSpace);
         CHECK(failedWith(fixture.fileSystem.writeFile("/a", bytes("abc")), Error::NoSpace));
      }
      SUBCASE("close error") {
         fixture.fileSystem.failNextClose(Error::Io);
         CHECK(failedWith(fixture.fileSystem.writeFile("/a", bytes("abc")), Error::Io));
      }
      CHECK(fixture.fileSystem.openFileCount() == 0u);
   }
}

TEST_SUITE("art::storage::FileSystem interface") {

   TEST_CASE("the helpers work through a base class reference") {
      MountedFileSystem fixture;
      FileSystem &fileSystem = fixture.fileSystem;
      CHECK(fileSystem.isMounted());
      REQUIRE(fileSystem.writeFile("/a", bytes("abc")).has_value());
      std::array<uint8_t, 8> buffer{};
      CHECK(fileSystem.readFile("/a", buffer).value() == 3u);
      CHECK(fileSystem.stat("/a").value().size == 3u);
   }

   TEST_CASE("unmount invalidates open files and is idempotent") {
      MountedFileSystem fixture;
      File file = openForWriting(fixture.fileSystem, "/a");
      CHECK(fixture.fileSystem.unmount().has_value());
      CHECK_FALSE(fixture.fileSystem.isMounted());
      CHECK(fixture.fileSystem.openFileCount() == 0u);
      CHECK(fixture.fileSystem.unmount().has_value());
   }

   TEST_CASE("format requires an unmounted file system") {
      MountedFileSystem fixture;
      CHECK(failedWith(fixture.fileSystem.format(), Error::Busy));
      REQUIRE(fixture.fileSystem.unmount().has_value());
      CHECK(fixture.fileSystem.format().has_value());
   }

   TEST_CASE("visitDirectory reports every entry and honours early stop") {
      MountedFileSystem fixture;
      REQUIRE(fixture.fileSystem.mkdir("/d").has_value());
      REQUIRE(fixture.fileSystem.writeFile("/d/one", bytes("1")).has_value());
      REQUIRE(fixture.fileSystem.writeFile("/d/two", bytes("22")).has_value());
      REQUIRE(fixture.fileSystem.mkdir("/d/sub").has_value());
      REQUIRE(fixture.fileSystem.writeFile("/other", bytes("x")).has_value());

      struct Collector {
         std::vector<std::string> names;
         offset_t totalSize{ 0u };
         size_t stopAfter{ 100u };

         bool visit(const char *name, const FileInfo &info) {
            names.emplace_back(name);
            totalSize += info.size;
            return names.size() < stopAfter;
         }
      };

      Collector all;
      const auto visited = fixture.fileSystem.visitDirectory("/d", DirectoryVisitor::create<Collector, &Collector::visit>(all));
      REQUIRE(visited.has_value());
      CHECK(visited.value() == 3u);
      CHECK(all.names.size() == 3u);
      CHECK(all.totalSize == 3u);

      Collector stopEarly;
      stopEarly.stopAfter = 1u;
      CHECK(fixture.fileSystem.visitDirectory("/d", DirectoryVisitor::create<Collector, &Collector::visit>(stopEarly)).value() == 1u);

      CHECK(failedWith(fixture.fileSystem.visitDirectory("/nope", DirectoryVisitor::create<Collector, &Collector::visit>(all)),
                       Error::NoEntry));
      CHECK(failedWith(fixture.fileSystem.visitDirectory("/other", DirectoryVisitor::create<Collector, &Collector::visit>(all)),
                       Error::NotDirectory));
   }

   TEST_CASE("rename replaces an existing file, which the temp-then-rename save pattern relies on") {
      MountedFileSystem fixture;
      REQUIRE(fixture.fileSystem.writeFile("/config", bytes("old")).has_value());
      REQUIRE(fixture.fileSystem.writeFile("/config.tmp", bytes("new")).has_value());
      REQUIRE(fixture.fileSystem.rename("/config.tmp", "/config").has_value());
      CHECK(fixture.fileSystem.content("/config") == bytes("new"));
      CHECK(failedWith(fixture.fileSystem.stat("/config.tmp"), Error::NoEntry));
   }

   TEST_CASE("remove distinguishes files, empty and non-empty directories") {
      MountedFileSystem fixture;
      REQUIRE(fixture.fileSystem.mkdir("/d").has_value());
      REQUIRE(fixture.fileSystem.writeFile("/d/a", bytes("x")).has_value());
      CHECK(failedWith(fixture.fileSystem.remove("/d"), Error::NotEmpty));
      CHECK(fixture.fileSystem.remove("/d/a").has_value());
      CHECK(fixture.fileSystem.remove("/d").has_value());
      CHECK(failedWith(fixture.fileSystem.remove("/d"), Error::NoEntry));
      CHECK(fixture.fileSystem.mkdir("/x").has_value());
      CHECK(failedWith(fixture.fileSystem.mkdir("/x"), Error::Exists));
   }
}
