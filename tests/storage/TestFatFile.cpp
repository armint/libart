#include "RamDisk.h"
#include "storage/FatFile.h"
#include "storage/fatfs/ff.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string_view>

// ---------------------------------------------------------------------------
// Fixture: formats and mounts a fresh FAT volume before each test case.
// ---------------------------------------------------------------------------
struct FsFixture {
   FATFS fatfs{};
   uint8_t workBuf[512]{};

   FsFixture() {
      RamDisk::instance().reset();
      MKFS_PARM opt{};
      opt.fmt = FM_ANY;
      REQUIRE(f_mkfs("0:", &opt, workBuf, sizeof(workBuf)) == FR_OK);
      REQUIRE(f_mount(&fatfs, "0:", 1) == FR_OK);
   }

   ~FsFixture() {
      f_unmount("0:");
   }
};

// ---------------------------------------------------------------------------
// Open / existence
// ---------------------------------------------------------------------------
TEST_CASE("FatFile: opening a non-existent file for reading fails", "[FatFile]") {
   FsFixture fs;
   FatFile f("0:/ghost.txt", FA_READ);
   CHECK_FALSE(f.exists());
}

TEST_CASE("FatFile: creating a new file succeeds", "[FatFile]") {
   FsFixture fs;
   FatFile f("0:/new.txt", FA_WRITE | FA_CREATE_ALWAYS);
   CHECK(f.exists());
}

// ---------------------------------------------------------------------------
// Write / read round-trip
// ---------------------------------------------------------------------------
TEST_CASE("FatFile: write then read back", "[FatFile]") {
   FsFixture fs;
   constexpr std::string_view msg = "Hello, FAT!";

   SECTION("write returns byte count") {
      FatFile f("0:/test.txt", FA_WRITE | FA_CREATE_ALWAYS);
      REQUIRE(f.exists());
      auto result = f.write({ reinterpret_cast<const uint8_t *>(msg.data()), msg.size() });
      REQUIRE(result.has_value());
      CHECK(*result == msg.size());
   }

   SECTION("data is readable after reopen") {
      {
         FatFile f("0:/test.txt", FA_WRITE | FA_CREATE_ALWAYS);
         REQUIRE(f.exists());
         f.write({ reinterpret_cast<const uint8_t *>(msg.data()), msg.size() });
      }

      FatFile f("0:/test.txt", FA_READ);
      REQUIRE(f.exists());
      std::array<uint8_t, 32> buf{};
      auto result = f.read(buf);
      REQUIRE(result.has_value());
      CHECK(*result == msg.size());
      CHECK(std::string_view(reinterpret_cast<char *>(buf.data()), *result) == msg);
   }
}

TEST_CASE("FatFile: partial reads fill the buffer with available bytes", "[FatFile]") {
   FsFixture fs;
   constexpr std::string_view msg = "ABCDE";

   {
      FatFile f("0:/data.txt", FA_WRITE | FA_CREATE_ALWAYS);
      f.write({ reinterpret_cast<const uint8_t *>(msg.data()), msg.size() });
   }

   FatFile f("0:/data.txt", FA_READ);
   REQUIRE(f.exists());

   std::array<uint8_t, 3> small{};
   auto r1 = f.read(small);
   REQUIRE(r1.has_value());
   CHECK(*r1 == 3);
   CHECK(small[0] == 'A');
   CHECK(small[2] == 'C');

   auto r2 = f.read(small); // only 2 bytes remain
   REQUIRE(r2.has_value());
   CHECK(*r2 == 2);
   CHECK(small[0] == 'D');
   CHECK(small[1] == 'E');

   // EOF: nothing left
   auto r3 = f.read(small);
   REQUIRE(r3.has_value());
   CHECK(*r3 == 0);
}

// ---------------------------------------------------------------------------
// size() and tell()
// ---------------------------------------------------------------------------
TEST_CASE("FatFile: size reports bytes written", "[FatFile]") {
   FsFixture fs;

   FatFile f("0:/sz.txt", FA_WRITE | FA_READ | FA_CREATE_ALWAYS);
   REQUIRE(f.exists());
   CHECK(f.size() == 0);

   constexpr std::string_view data = "1234567890";
   f.write({ reinterpret_cast<const uint8_t *>(data.data()), data.size() });
   CHECK(f.size() == data.size());
}

TEST_CASE("FatFile: tell tracks write position", "[FatFile]") {
   FsFixture fs;
   FatFile f("0:/tell.txt", FA_WRITE | FA_CREATE_ALWAYS);
   REQUIRE(f.exists());
   CHECK(f.tell() == 0);

   constexpr std::string_view data = "HELLO";
   f.write({ reinterpret_cast<const uint8_t *>(data.data()), data.size() });
   CHECK(f.tell() == 5);
}

// ---------------------------------------------------------------------------
// seek()
// ---------------------------------------------------------------------------
TEST_CASE("FatFile: seek changes read position", "[FatFile]") {
   FsFixture fs;
   constexpr std::string_view msg = "ABCDEFGH";

   {
      FatFile f("0:/seek.txt", FA_WRITE | FA_CREATE_ALWAYS);
      f.write({ reinterpret_cast<const uint8_t *>(msg.data()), msg.size() });
   }

   FatFile f("0:/seek.txt", FA_READ);
   REQUIRE(f.exists());

   SECTION("seek to middle") {
      auto res = f.seek(3);
      REQUIRE(res.has_value());
      CHECK(f.tell() == 3);

      std::array<uint8_t, 1> ch{};
      auto r = f.read(ch);
      REQUIRE(r.has_value());
      CHECK(ch[0] == 'D');
   }

   SECTION("seek to beginning") {
      f.seek(5);
      f.seek(0);
      CHECK(f.tell() == 0);
   }

   SECTION("seek to end equals size") {
      f.seek(msg.size());
      CHECK(f.tell() == msg.size());
   }
}

// ---------------------------------------------------------------------------
// flush()
// ---------------------------------------------------------------------------
TEST_CASE("FatFile: flush returns success on open file", "[FatFile]") {
   FsFixture fs;
   FatFile f("0:/flush.txt", FA_WRITE | FA_CREATE_ALWAYS);
   REQUIRE(f.exists());
   f.write({ reinterpret_cast<const uint8_t *>("data"), 4 });
   auto res = f.flush();
   CHECK(res.has_value());
}

// ---------------------------------------------------------------------------
// Multiple files
// ---------------------------------------------------------------------------
TEST_CASE("FatFile: multiple files coexist on the volume", "[FatFile]") {
   FsFixture fs;

   {
      FatFile a("0:/a.txt", FA_WRITE | FA_CREATE_ALWAYS);
      a.write({ reinterpret_cast<const uint8_t *>("AAA"), 3 });
   }
   {
      FatFile b("0:/b.txt", FA_WRITE | FA_CREATE_ALWAYS);
      b.write({ reinterpret_cast<const uint8_t *>("BBBB"), 4 });
   }

   FatFile ra("0:/a.txt", FA_READ);
   FatFile rb("0:/b.txt", FA_READ);
   CHECK(ra.size() == 3);
   CHECK(rb.size() == 4);
}
