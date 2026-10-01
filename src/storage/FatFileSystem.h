#pragma once
#include "fatfs/ff.h"
#include "storage/BlockDevice.h"
#include "utils/compat/expected.h"
#include <optional>

#include "fatfs/diskio.h"


/**
 * @brief FatFs volume on top of an art::storage::BlockDevice.
 *
 * The constructor registers the device as the next free FatFs drive. diskio.cpp, which FatFs calls for every
 * sector access, translates the sector numbers of FatFs into byte offsets of the device. The device must
 * outlive the FatFileSystem and, as any BlockDevice, must not be used by another caller while FatFs owns it.
 *
 * Requirements on the device geometry, checked when FatFs initialises the drive (mount, format): the geometry
 * is valid, readSize and programSize divide the FAT sector size (FF_MIN_SS), and the capacity is at least one
 * and at most 2^32 - 1 sectors. Otherwise the drive stays not ready and FatFs reports FR_NOT_READY.
 */
class FatFileSystem {
   // State of a registered drive. `initialised` mirrors the STA_NOINIT bit of FatFs: it is set by a successful
   // BlockDevice::init() and cleared when the device fails one, or reports that it is no longer ready.
   struct Drive {
      art::storage::BlockDevice *device;
      bool initialised;
   };

   static std::optional<Drive> devices[FF_VOLUMES];
   // The registered drive, or nullptr if `pdrv` is out of range or unused.
   static Drive *drive(BYTE pdrv);
   unsigned int _driveNumber;

   FATFS _fatfs{};

   // Disk I/O functions residing in diskio.cpp
   friend DSTATUS disk_initialize(BYTE pdrv);
   friend DSTATUS disk_status(BYTE pdrv);
   friend DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
   friend DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
   friend DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

   public:
   FatFileSystem(art::storage::BlockDevice &blockDevice);
   ~FatFileSystem();
   inline unsigned int getDriveNumber() const {
      return _driveNumber;
   }
   FRESULT mount();
   art::expected<uint64_t, FRESULT> free();
   FRESULT remove(const char *fileName);
};
