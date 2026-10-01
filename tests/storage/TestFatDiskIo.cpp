#include "BlockDeviceContract.h"
#include "FatTestDisk.h"
#include "storage/BlockDevice.h"
#include "storage/FatFileSystem.h"
#include "storage/fatfs/ff.h"
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <limits>
#include <string_view>

using namespace art::storage;
using namespace art::storage::test;

namespace {

// BlockDevice that only records what diskio.cpp asks of it and answers with scripted results.
class ScriptedDevice : public BlockDevice {
   public:
   explicit ScriptedDevice(const Geometry &geometry)
      : geometry_(geometry) {}

   art::expected<void, Error> init() override {
      ++initCount;
      return initResult;
   }

   art::expected<void, Error> read(offset_t offset, art::span<uint8_t> buffer) override {
      ++readCount;
      lastOffset = offset;
      lastLength = buffer.size();
      return ioResult;
   }

   art::expected<void, Error> program(offset_t offset, art::span<const uint8_t> data) override {
      ++programCount;
      lastOffset = offset;
      lastLength = data.size();
      return ioResult;
   }

   art::expected<void, Error> erase(offset_t offset, offset_t length) override {
      ++eraseCount;
      lastOffset = offset;
      lastLength = length;
      return ioResult;
   }

   art::expected<void, Error> sync() override {
      ++syncCount;
      return syncResult;
   }

   Geometry geometry() const override {
      return geometry_;
   }

   Geometry geometry_;
   art::expected<void, Error> initResult{};
   art::expected<void, Error> ioResult{};
   art::expected<void, Error> syncResult{};
   uint32_t initCount{ 0u };
   uint32_t readCount{ 0u };
   uint32_t programCount{ 0u };
   uint32_t eraseCount{ 0u };
   uint32_t syncCount{ 0u };
   offset_t lastOffset{ 0u };
   offset_t lastLength{ 0u };
};

// clang-format off
// SD card: 512 byte sectors, overwritable, no erase unit reported, 1 MiB.
constexpr Geometry kSdCard{
   .readSize = 512u, .programSize = 512u, .eraseSize = 0u, .size = 1024u * 1024u, .eraseBeforeProgram = false
};
// clang-format on

// Registers a ScriptedDevice as drive 0, as the fixtures of the FatFs tests do for a real one.
struct DriveFixture {
   explicit DriveFixture(const Geometry &geometry = kSdCard)
      : device(geometry) {}

   ScriptedDevice device;
   FatFileSystem fatFileSystem{ device };
   std::array<uint8_t, 4096> buffer{};

   // Puts the drive in the initialised state.
   void initialise() {
      REQUIRE(disk_initialize(0) == 0);
   }
};

} // namespace

TEST_CASE("[FatDiskIo] a drive is not ready until it is initialised [FatDiskIo]") {
   DriveFixture drive;
   CHECK(disk_status(0) == STA_NOINIT);
   CHECK(drive.device.initCount == 0u);
   CHECK(disk_initialize(0) == 0);
   CHECK(disk_status(0) == 0);
   CHECK(drive.device.initCount == 1u);
}

TEST_CASE("[FatDiskIo] an unregistered drive is not ready and rejects every call [FatDiskIo]") {
   uint8_t data[512]{};
   CHECK(disk_status(0) == STA_NOINIT);
   CHECK(disk_initialize(0) == STA_NOINIT);
   CHECK(disk_read(0, data, 0, 1) == RES_PARERR);
   CHECK(disk_write(0, data, 0, 1) == RES_PARERR);
   CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_PARERR);
   CHECK(disk_status(FF_VOLUMES) == STA_NOINIT);
}

TEST_CASE("[FatDiskIo] a failed init leaves the drive not ready and a later init recovers it [FatDiskIo]") {
   DriveFixture drive;
   drive.device.initResult = art::unexpected(Error::NotReady);
   CHECK(disk_initialize(0) == STA_NOINIT);
   CHECK(disk_status(0) == STA_NOINIT);

   drive.device.initResult = {};
   CHECK(disk_initialize(0) == 0);
   CHECK(disk_status(0) == 0);

   drive.device.initResult = art::unexpected(Error::Io);
   CHECK(disk_initialize(0) == STA_NOINIT);
   CHECK(disk_status(0) == STA_NOINIT);
}

TEST_CASE("[FatDiskIo] a geometry that cannot carry FAT sectors is refused [FatDiskIo]") {
   Geometry geometry = kSdCard;

   SUBCASE("read unit larger than a sector") {
      geometry.readSize = 1024u;
   }
   SUBCASE("program unit larger than a sector") {
      geometry.programSize = 4096u;
   }
   SUBCASE("program unit that does not divide a sector") {
      geometry.programSize = 768u;
      geometry.size = 3u * 1024u * 1024u;
   }
   SUBCASE("smaller than one sector") {
      geometry = Geometry{ .readSize = 1u, .programSize = 1u, .eraseSize = 0u, .size = 256u };
   }
   SUBCASE("invalid geometry") {
      geometry.size = 0u;
   }
   SUBCASE("more sectors than a 32 bit LBA") {
      geometry.size = (uint64_t{ 1 } << 41) + 512u;
   }
   DriveFixture drive{ geometry };
   CHECK(disk_initialize(0) == STA_NOINIT);
   CHECK(disk_status(0) == STA_NOINIT);
   uint8_t data[512]{};
   CHECK(disk_read(0, data, 0, 1) == RES_NOTRDY);
   CHECK(drive.device.readCount == 0u);
}

TEST_CASE("[FatDiskIo] a device with byte-sized units is accepted [FatDiskIo]") {
   // clang-format off
   constexpr Geometry nor{
      .readSize = 1u, .programSize = 4u, .eraseSize = 4096u, .size = 65536u, .eraseBeforeProgram = true
   };
   // clang-format on
   DriveFixture drive{ nor };
   CHECK(disk_initialize(0) == 0);
}

TEST_CASE("[FatDiskIo] access before initialisation is not ready and does not reach the device [FatDiskIo]") {
   DriveFixture drive;
   CHECK(disk_read(0, drive.buffer.data(), 0, 1) == RES_NOTRDY);
   CHECK(disk_write(0, drive.buffer.data(), 0, 1) == RES_NOTRDY);
   CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_NOTRDY);
   CHECK(drive.device.readCount + drive.device.programCount + drive.device.syncCount == 0u);
}

TEST_CASE("[FatDiskIo] sector numbers and counts become byte offsets and lengths [FatDiskIo]") {
   DriveFixture drive;
   drive.initialise();

   CHECK(disk_write(0, drive.buffer.data(), 3, 2) == RES_OK);
   CHECK(drive.device.lastOffset == 3u * 512u);
   CHECK(drive.device.lastLength == 2u * 512u);

   CHECK(disk_read(0, drive.buffer.data(), 7, 1) == RES_OK);
   CHECK(drive.device.lastOffset == 7u * 512u);
   CHECK(drive.device.lastLength == 512u);
}

TEST_CASE("[FatDiskIo] the byte offset of a high sector does not wrap at 32 bits [FatDiskIo]") {
   constexpr uint32_t kLastSector = std::numeric_limits<uint32_t>::max();
   Geometry geometry = kSdCard;
   geometry.size = (uint64_t{ kLastSector } + 1u) * 512u - 512u;
   DriveFixture drive{ geometry };
   drive.initialise();

   CHECK(disk_read(0, drive.buffer.data(), kLastSector, 1) == RES_OK);
   CHECK(drive.device.lastOffset == uint64_t{ kLastSector } * 512u);
   CHECK(drive.device.lastOffset > std::numeric_limits<uint32_t>::max());
}

TEST_CASE("[FatDiskIo] invalid arguments are rejected before the device is called [FatDiskIo]") {
   DriveFixture drive;
   drive.initialise();
   CHECK(disk_read(0, drive.buffer.data(), 0, 0) == RES_PARERR);
   CHECK(disk_write(0, drive.buffer.data(), 0, 0) == RES_PARERR);
   CHECK(disk_read(0, nullptr, 0, 1) == RES_PARERR);
   CHECK(disk_write(0, nullptr, 0, 1) == RES_PARERR);
   CHECK(drive.device.readCount + drive.device.programCount == 0u);
}

TEST_CASE("[FatDiskIo] device errors map onto DRESULT [FatDiskIo]") {
   struct Row {
      Error error;
      DRESULT expected;
   };
   // Every enumerator is listed on purpose: a new Error must get a decision here.
   constexpr Row rows[]{
      { Error::Io, RES_ERROR },
      { Error::Corrupt, RES_ERROR },
      { Error::NoEntry, RES_ERROR },
      { Error::Exists, RES_ERROR },
      { Error::NotDirectory, RES_ERROR },
      { Error::IsDirectory, RES_ERROR },
      { Error::NotEmpty, RES_ERROR },
      { Error::NameTooLong, RES_ERROR },
      { Error::NoSpace, RES_ERROR },
      { Error::NoMemory, RES_ERROR },
      { Error::TooBig, RES_ERROR },
      { Error::InvalidArgument, RES_PARERR },
      { Error::BadHandle, RES_ERROR },
      { Error::NotMounted, RES_ERROR },
      { Error::NotInitialised, RES_NOTRDY },
      { Error::Unsupported, RES_ERROR },
      { Error::Timeout, RES_ERROR },
      { Error::Busy, RES_ERROR },
      { Error::WriteProtected, RES_WRPRT },
      { Error::NotReady, RES_NOTRDY },
      { Error::NeedsRemount, RES_ERROR },
   };
   static_assert(std::size(rows) == static_cast<size_t>(Error::NeedsRemount) + 1u);

   for(const Row &row : rows) {
      CAPTURE(toString(row.error));
      DriveFixture drive;
      drive.initialise();
      drive.device.ioResult = art::unexpected(row.error);
      CHECK(disk_read(0, drive.buffer.data(), 0, 1) == row.expected);
      CHECK(disk_write(0, drive.buffer.data(), 0, 1) == row.expected);
   }
}

TEST_CASE("[FatDiskIo] a device that reports it is gone leaves the drive not ready until it is initialised again "
          "[FatDiskIo]") {
   DriveFixture drive;
   drive.initialise();

   SUBCASE("hard failure keeps the drive ready") {
      drive.device.ioResult = art::unexpected(Error::Io);
      CHECK(disk_read(0, drive.buffer.data(), 0, 1) == RES_ERROR);
      CHECK(disk_status(0) == 0);
   }
   SUBCASE("write protection keeps the drive ready") {
      drive.device.ioResult = art::unexpected(Error::WriteProtected);
      CHECK(disk_write(0, drive.buffer.data(), 0, 1) == RES_WRPRT);
      CHECK(disk_status(0) == 0);
   }
   SUBCASE("not ready on read") {
      drive.device.ioResult = art::unexpected(Error::NotReady);
      CHECK(disk_read(0, drive.buffer.data(), 0, 1) == RES_NOTRDY);
      CHECK(disk_status(0) == STA_NOINIT);
      CHECK(disk_write(0, drive.buffer.data(), 0, 1) == RES_NOTRDY);
      CHECK(drive.device.programCount == 0u);

      drive.device.ioResult = {};
      CHECK(disk_initialize(0) == 0);
      CHECK(disk_read(0, drive.buffer.data(), 0, 1) == RES_OK);
   }
   SUBCASE("not initialised on write") {
      drive.device.ioResult = art::unexpected(Error::NotInitialised);
      CHECK(disk_write(0, drive.buffer.data(), 0, 1) == RES_NOTRDY);
      CHECK(disk_status(0) == STA_NOINIT);
   }
   SUBCASE("not ready on sync") {
      drive.device.syncResult = art::unexpected(Error::NotReady);
      CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_NOTRDY);
      CHECK(disk_status(0) == STA_NOINIT);
   }
}

TEST_CASE("[FatDiskIo] CTRL_SYNC flushes the device [FatDiskIo]") {
   DriveFixture drive;
   drive.initialise();
   CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_OK);
   CHECK(drive.device.syncCount == 1u);

   drive.device.syncResult = art::unexpected(Error::Io);
   CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_ERROR);
}

TEST_CASE("[FatDiskIo] GET_SECTOR_COUNT and GET_BLOCK_SIZE report the geometry in sectors [FatDiskIo]") {
   Geometry geometry = kSdCard;
   uint32_t eraseBlockSectors = 99u;

   SUBCASE("no erase unit is reported as unknown") {
      DriveFixture drive{ geometry };
      drive.initialise();
      LBA_t sectors = 0;
      CHECK(disk_ioctl(0, GET_SECTOR_COUNT, &sectors) == RES_OK);
      CHECK(sectors == 2048u);
      CHECK(disk_ioctl(0, GET_BLOCK_SIZE, &eraseBlockSectors) == RES_OK);
      CHECK(eraseBlockSectors == 0u);
   }
   SUBCASE("erase unit is converted from bytes to sectors") {
      geometry.eraseSize = 4096u;
      DriveFixture drive{ geometry };
      drive.initialise();
      CHECK(disk_ioctl(0, GET_BLOCK_SIZE, &eraseBlockSectors) == RES_OK);
      CHECK(eraseBlockSectors == 8u);
   }
   SUBCASE("unsupported command") {
      DriveFixture drive{ geometry };
      drive.initialise();
      CHECK(disk_ioctl(0, CTRL_EJECT, nullptr) == RES_PARERR);
   }
}

TEST_CASE("[FatDiskIo] CTRL_TRIM erases only the whole erase units inside the range [FatDiskIo]") {
   Geometry geometry = kSdCard;
   geometry.eraseSize = 4096u;
   DriveFixture drive{ geometry };
   drive.initialise();

   SUBCASE("aligned range") {
      LBA_t range[2]{ 8, 23 }; // inclusive: sectors 8..23 are bytes 4096..12287
      CHECK(disk_ioctl(0, CTRL_TRIM, range) == RES_OK);
      CHECK(drive.device.eraseCount == 1u);
      CHECK(drive.device.lastOffset == 4096u);
      CHECK(drive.device.lastLength == 8192u);
   }
   SUBCASE("range is shrunk to the units it contains") {
      LBA_t range[2]{ 5, 20 }; // bytes 2560..10751 contain only the unit 4096..8191
      CHECK(disk_ioctl(0, CTRL_TRIM, range) == RES_OK);
      CHECK(drive.device.lastOffset == 4096u);
      CHECK(drive.device.lastLength == 4096u);
   }
   SUBCASE("range without a whole unit is a no-op") {
      LBA_t range[2]{ 1, 6 };
      CHECK(disk_ioctl(0, CTRL_TRIM, range) == RES_OK);
      CHECK(drive.device.eraseCount == 0u);
   }
}

TEST_CASE("[FatDiskIo] CTRL_TRIM on a device without erase unit has nothing to do [FatDiskIo]") {
   DriveFixture drive;
   drive.initialise();
   LBA_t range[2]{ 0, 7 };
   CHECK(disk_ioctl(0, CTRL_TRIM, range) == RES_OK);
   CHECK(drive.device.eraseCount == 0u);
}

TEST_CASE("[FatDiskIo] a FAT volume lands on the medium at sector times 512 [FatDiskIo]") {
   FatTestDisk disk;
   FatFileSystem fatFileSystem{ disk };
   FATFS fatfs{};
   uint8_t workBuf[512]{};

   SUBCASE("mounting a blank device reports no file system") {
      CHECK(f_mount(&fatfs, "0:", 1) == FR_NO_FILESYSTEM);
   }
   SUBCASE("format, write and read back") {
      MKFS_PARM opt{};
      opt.fmt = FM_ANY;
      REQUIRE(f_mkfs("0:", &opt, workBuf, sizeof(workBuf)) == FR_OK);
      REQUIRE(f_mount(&fatfs, "0:", 1) == FR_OK);

      // The boot sector is sector 0, and its signature sits at byte 510 of it.
      CHECK(disk.contents()[510] == 0x55);
      CHECK(disk.contents()[511] == 0xAA);

      FIL file;
      REQUIRE(f_open(&file, "0:/a.txt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
      UINT written = 0;
      REQUIRE(f_write(&file, "hello", 5, &written) == FR_OK);
      REQUIRE(f_close(&file) == FR_OK);
      CHECK(written == 5u);

      REQUIRE(f_open(&file, "0:/a.txt", FA_READ) == FR_OK);
      char text[8]{};
      UINT read = 0;
      CHECK(f_read(&file, text, sizeof(text), &read) == FR_OK);
      CHECK(read == 5u);
      CHECK(std::string_view(text, read) == "hello");
      f_close(&file);

      const auto freeBytes = fatFileSystem.free();
      REQUIRE(freeBytes.has_value());
      CHECK(*freeBytes > 0u);
      CHECK(*freeBytes < FatTestDisk::kSize);
      CHECK(*freeBytes % FatTestDisk::kSectorSize == 0u);
      f_unmount("0:");
   }
}

TEST_CASE("[FatDiskIo] a 512 byte sector SD-like device passes the BlockDevice contract [FatDiskIo]") {
   FatTestDisk disk;
   checkBlockDeviceContract(disk, RamBlockDevice::kErasedByte);
}
