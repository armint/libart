#pragma once

#include "RamBlockDevice.h"
#include "storage/FileSystem.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <vector>

// The volume that TestLittleFsCompat.cpp mounts, and the code that writes and checks it. The committed image
// (fixtures/littlefs-v2.9.3.img) was written by populateCompatVolume() on a build against littlefs v2.9.3; see the
// header of TestLittleFsCompat.cpp for how to write a new one.
//
// The content is chosen to reach every kind of structure littlefs puts on the medium: inline files, files in a
// chain of data blocks with skip pointers, a file that was overwritten, directories, an entry that was moved to
// another directory, deleted entries in a metadata log and a directory whose log was compacted.
namespace art::storage::test::compat {

// 24 blocks of 512 bytes. The default LittleFsFileSystem cache (256 bytes) divides the erase unit.
// clang-format off
inline constexpr Geometry kGeometry{
   .readSize = 16u, .programSize = 16u, .eraseSize = 512u, .size = 24u * 512u, .eraseBeforeProgram = true
};
// clang-format on

inline std::vector<uint8_t> pattern(size_t size, uint8_t seed) {
   std::vector<uint8_t> data(size);
   for(size_t i = 0; i < size; ++i) {
      data[i] = static_cast<uint8_t>((i * 31u + seed) ^ (i >> 8u));
   }
   return data;
}

inline std::vector<uint8_t> text(const std::string &value) {
   return { value.begin(), value.end() };
}

struct Entry {
   const char *path;
   std::vector<uint8_t> content;
};

/// The files of the volume and their content. Directories are /notes and /data.
inline std::vector<Entry> files() {
   return { { "/hello.txt", text("hello from littlefs\n") },
            { "/notes/empty.txt", {} },
            { "/notes/inline.bin", pattern(40u, 3u) },
            { "/data/blob.bin", pattern(2600u, 4u) },   // six data blocks, so the skip pointers are used
            { "/data/moved.bin", pattern(700u, 5u) } }; // created in /notes, then renamed into /data
}

/// Writes the volume onto an empty, mounted file system.
inline void populateCompatVolume(FileSystem &fileSystem) {
   REQUIRE(fileSystem.writeFile("/hello.txt", text("hello from littlefs\n")).has_value());
   REQUIRE(fileSystem.mkdir("/notes").has_value());
   REQUIRE(fileSystem.mkdir("/data").has_value());
   REQUIRE(fileSystem.writeFile("/notes/empty.txt", {}).has_value());
   REQUIRE(fileSystem.writeFile("/notes/inline.bin", pattern(40u, 3u)).has_value());
   REQUIRE(fileSystem.writeFile("/data/blob.bin", pattern(900u, 9u)).has_value());
   REQUIRE(fileSystem.writeFile("/data/blob.bin", pattern(2600u, 4u)).has_value()); // overwritten and longer
   REQUIRE(fileSystem.writeFile("/notes/moved.bin", pattern(700u, 5u)).has_value());
   REQUIRE(fileSystem.rename("/notes/moved.bin", "/data/moved.bin").has_value());
   // Deleted entries in the metadata log, and enough of them to compact it.
   for(uint8_t cycle = 0u; cycle < 25u; ++cycle) {
      REQUIRE(fileSystem.writeFile("/notes/scratch", pattern(24u, cycle)).has_value());
      REQUIRE(fileSystem.remove("/notes/scratch").has_value());
   }
}

inline std::vector<uint8_t> readAll(FileSystem &fileSystem, const char *path) {
   std::array<uint8_t, 4096> buffer{};
   const auto count = fileSystem.readFile(path, buffer);
   REQUIRE(count.has_value());
   return { buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*count) };
}

inline std::vector<std::string> list(FileSystem &fileSystem, const char *path) {
   struct Names {
      std::vector<std::string> names;
      bool visit(const char *name, const FileInfo &info) {
         names.push_back(std::string(name) + (info.type == EntryType::Directory ? "/" : ""));
         return true;
      }
   } listed;
   REQUIRE(fileSystem.visitDirectory(path, DirectoryVisitor::create<Names, &Names::visit>(listed)).has_value());
   return listed.names;
}

/// Checks that the volume holds exactly what populateCompatVolume() wrote.
inline void verifyCompatVolume(FileSystem &fileSystem) {
   CHECK(list(fileSystem, "/") == std::vector<std::string>{ "data/", "hello.txt", "notes/" });
   CHECK(list(fileSystem, "/notes") == std::vector<std::string>{ "empty.txt", "inline.bin" });
   CHECK(list(fileSystem, "/data") == std::vector<std::string>{ "blob.bin", "moved.bin" });
   for(const auto &entry : files()) {
      CAPTURE(entry.path);
      CHECK(readAll(fileSystem, entry.path) == entry.content);
      const auto info = fileSystem.stat(entry.path);
      REQUIRE(info.has_value());
      CHECK(info->size == entry.content.size());
   }
}

} // namespace art::storage::test::compat
