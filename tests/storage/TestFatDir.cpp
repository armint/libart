#include "RamDisk.h"
#include "storage/FatDir.h"
#include "storage/FatFileSystem.h"
#include "storage/fatfs/ff.h"
#include <array>
#include <doctest/doctest.h>

// ---------------------------------------------------------------------------
// Fixture: formats and mounts a fresh FAT volume before each test case.
// ---------------------------------------------------------------------------
struct DirFsFixture {
   // Registers RamDisk::instance() as drive 0 in FatFileSystem::devices[],
   // which diskio.cpp's disk_*() calls route through; without this,
   // f_mkfs()/f_mount() below fail with FR_NOT_READY.
   FatFileSystem fatFileSystem{ RamDisk::instance() };
   FATFS fatfs{};
   uint8_t workBuf[512]{};

   DirFsFixture() {
      RamDisk::instance().reset();
      MKFS_PARM opt{};
      opt.fmt = FM_ANY;
      REQUIRE(f_mkfs("0:", &opt, workBuf, sizeof(workBuf)) == FR_OK);
      REQUIRE(f_mount(&fatfs, "0:", 1) == FR_OK);
   }

   ~DirFsFixture() {
      f_unmount("0:");
   }

   // Helper: create an empty file at the given path.
   void touch(const char *path) {
      FIL fil;
      f_open(&fil, path, FA_WRITE | FA_CREATE_ALWAYS);
      f_close(&fil);
   }
};

// ---------------------------------------------------------------------------
// Open / existence
// ---------------------------------------------------------------------------
TEST_CASE("[FatDir] opening root directory succeeds [FatDir]") {
   DirFsFixture fs;
   FatDir dir("0:/");
   CHECK(dir.exists());
}

TEST_CASE("[FatDir] opening a non-existent directory fails [FatDir]") {
   DirFsFixture fs;
   FatDir dir("0:/nosuchdir");
   CHECK_FALSE(dir.exists());
}

TEST_CASE("[FatDir] opening an explicitly created subdirectory succeeds [FatDir]") {
   DirFsFixture fs;
   REQUIRE(f_mkdir("0:/subdir") == FR_OK);
   FatDir dir("0:/subdir");
   CHECK(dir.exists());
}

// ---------------------------------------------------------------------------
// tell()
// ---------------------------------------------------------------------------
TEST_CASE("[FatDir] tell returns 0 on freshly opened directory [FatDir]") {
   DirFsFixture fs;
   FatDir dir("0:/");
   REQUIRE(dir.exists());
   CHECK(dir.tell() == 0);
}

// ---------------------------------------------------------------------------
// read() — currently an unimplemented stub
// ---------------------------------------------------------------------------
TEST_CASE("[FatDir] read returns FR_DENIED (stub not yet implemented) [FatDir]") {
   DirFsFixture fs;
   FatDir dir("0:/");
   REQUIRE(dir.exists());
   FILINFO info{};
   // FatDir::read() is a known stub that always returns FR_DENIED.
   CHECK(dir.read(info) == FR_DENIED);
}

// ---------------------------------------------------------------------------
// seek()
// ---------------------------------------------------------------------------
TEST_CASE("[FatDir] seek on empty directory to offset 0 succeeds [FatDir]") {
   DirFsFixture fs;
   FatDir dir("0:/");
   REQUIRE(dir.exists());
   // Seeking to 0 on an empty-ish root just rewinds (no entries to skip).
   CHECK(dir.seek(0) == FR_OK);
   CHECK(dir.tell() == 0);
}

TEST_CASE("[FatDir] seek advances past directory entries [FatDir]") {
   DirFsFixture fs;
   // Use short 8.3 names so each entry occupies exactly one 32-byte slot.
   fs.touch("0:/A.TXT");
   fs.touch("0:/B.TXT");
   fs.touch("0:/C.TXT");

   FatDir dir("0:/");
   REQUIRE(dir.exists());

   // seek() advances the directory pointer by reading entries until dptr >= offset.
   // Each 8.3 entry in root takes 32 bytes, so dptr after one entry is 32.
   REQUIRE(dir.seek(32) == FR_OK);
   size_t pos = dir.tell();
   CHECK(pos >= 32);

   SUBCASE("seek back to start rewinds") {
      REQUIRE(dir.seek(0) == FR_OK);
      CHECK(dir.tell() == 0);
   }

   SUBCASE("seek past all entries returns FR_NO_PATH") {
      // Seeking far beyond the actual number of entries should return FR_NO_PATH.
      FRESULT res = dir.seek(0xFFFFFF);
      CHECK(res == FR_NO_PATH);
   }
}

TEST_CASE("[FatDir] seek on subdirectory works [FatDir]") {
   DirFsFixture fs;
   REQUIRE(f_mkdir("0:/mydir") == FR_OK);
   fs.touch("0:/mydir/X.TXT");
   fs.touch("0:/mydir/Y.TXT");

   FatDir dir("0:/mydir");
   REQUIRE(dir.exists());
   CHECK(dir.tell() == 0);

   REQUIRE(dir.seek(32) == FR_OK);
   CHECK(dir.tell() >= 32);
}
