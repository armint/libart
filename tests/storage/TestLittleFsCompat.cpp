#include "LittleFsCompatFixture.h"
#include "RamBlockDevice.h"
#include "storage/littlefs/LittleFsFileSystem.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// On-disk compatibility of the littlefs pin. A node that receives a firmware with a newer littlefs mounts the
// volume that the older one wrote, and mount() formats a volume that littlefs calls corrupt, so an incompatibility
// would silently wipe the node's data. The image in fixtures/ was written by v2.9.3, the version pinned before
// v2.11.3, and must mount and read back unchanged. The same must hold for every later bump: keep the old image and
// add the image of the version you leave.
//
// Writing an image (the only way to do it; nothing in the build does):
//   1. Configure a native build with -DFETCHCONTENT_SOURCE_DIR_LITTLEFS=<checkout of the littlefs tag to write with>.
//   2. Run: LITTLEFS_FIXTURE_OUT=<file> libArT-tests --no-skip -tc="write the littlefs compatibility image"
// Volume and geometry are in LittleFsCompatFixture.h. Do not change them without writing new images.

using namespace art::storage;
using namespace art::storage::test;
using namespace art::storage::test::compat;

#ifndef LITTLEFS_FIXTURE_DIR
#error "tests/CMakeLists.txt must define LITTLEFS_FIXTURE_DIR"
#endif

namespace {

std::vector<uint8_t> loadImage(const std::string &name) {
   std::ifstream stream(std::string(LITTLEFS_FIXTURE_DIR) + "/" + name, std::ios::binary);
   REQUIRE_MESSAGE(stream.is_open(), "fixture image " << name << " not found in " << LITTLEFS_FIXTURE_DIR);
   return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

} // namespace

TEST_SUITE("LittleFsFileSystem on-disk compatibility") {

   TEST_CASE("a volume written by littlefs v2.9.3 mounts, reads back and stays writable") {
      const auto image = loadImage("littlefs-v2.9.3.img");
      REQUIRE(image.size() == kGeometry.size);
      std::array<uint8_t, kGeometry.size> backing{};
      RamBlockDevice ram(kGeometry, backing);
      std::copy(image.begin(), image.end(), backing.begin());
      const std::array<uint8_t, kGeometry.size> original = backing;

      LittleFsFileSystem<> fileSystem(ram);
      REQUIRE(fileSystem.mount().has_value());
      CHECK(ram.eraseCount() == 0u); // mounted as it is; a format would erase
      verifyCompatVolume(fileSystem);
      CHECK(ram.programCount() == 0u); // reading changes nothing on the medium
      CHECK(backing == original);

      // The old volume takes new writes of every kind, and the result survives a remount.
      const auto more = pattern(1800u, 11u);
      REQUIRE(fileSystem.mkdir("/added").has_value());
      REQUIRE(fileSystem.writeFile("/added/more.bin", more).has_value());
      REQUIRE(fileSystem.writeFile("/data/blob.bin", pattern(300u, 12u)).has_value());
      REQUIRE(fileSystem.rename("/data/moved.bin", "/added/moved.bin").has_value());
      REQUIRE(fileSystem.remove("/hello.txt").has_value());
      REQUIRE(fileSystem.unmount().has_value());
      REQUIRE(fileSystem.mount().has_value());
      CHECK(readAll(fileSystem, "/added/more.bin") == more);
      CHECK(readAll(fileSystem, "/data/blob.bin") == pattern(300u, 12u));
      CHECK(readAll(fileSystem, "/added/moved.bin") == pattern(700u, 5u));
      CHECK(readAll(fileSystem, "/notes/inline.bin") == pattern(40u, 3u));
      CHECK((fileSystem.stat("/hello.txt").error() == Error::NoEntry));
      CHECK(list(fileSystem, "/") == std::vector<std::string>{ "added/", "data/", "notes/" });
   }

   TEST_CASE("the volume of the compatibility fixture is what the generator writes") {
      // Guards the generator and the checker against each other on the pinned version; the image itself is proven
      // by the test above.
      std::array<uint8_t, kGeometry.size> backing{};
      RamBlockDevice ram(kGeometry, backing);
      LittleFsFileSystem<> fileSystem(ram);
      REQUIRE(fileSystem.mount().has_value());
      populateCompatVolume(fileSystem);
      verifyCompatVolume(fileSystem);
      REQUIRE(fileSystem.unmount().has_value());
      REQUIRE(fileSystem.mount().has_value());
      verifyCompatVolume(fileSystem);
   }

   TEST_CASE("write the littlefs compatibility image" * doctest::skip()) {
      const char *const output = std::getenv("LITTLEFS_FIXTURE_OUT");
      REQUIRE_MESSAGE(output != nullptr, "set LITTLEFS_FIXTURE_OUT to the file to write");
      std::array<uint8_t, kGeometry.size> backing{};
      RamBlockDevice ram(kGeometry, backing);
      LittleFsFileSystem<> fileSystem(ram);
      REQUIRE(fileSystem.mount().has_value());
      populateCompatVolume(fileSystem);
      verifyCompatVolume(fileSystem);
      REQUIRE(fileSystem.unmount().has_value());
      std::ofstream stream(output, std::ios::binary);
      REQUIRE(stream.is_open());
      stream.write(reinterpret_cast<const char *>(backing.data()), static_cast<std::streamsize>(backing.size()));
      REQUIRE(stream.good());
   }
}
