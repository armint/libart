#pragma once
#include "StorageDevice.h"
#include "fatfs/ff.h"
#include <expected>
#include <optional>

#include "fatfs/diskio.h"


class FatFileSystem {
   static std::optional<StorageDevice *> devices[FF_VOLUMES];
   StorageDevice *_sdDevice;
   unsigned int _driveNumber;

   FATFS _fatfs{};

   // Disk I/O functions residing in diskio.cpp
   friend DSTATUS disk_initialize(BYTE pdrv);
   friend DSTATUS disk_status(BYTE pdrv);
   friend DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
   friend DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
   friend DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

   public:
   FatFileSystem(StorageDevice &sdDevice);
   ~FatFileSystem();
   inline unsigned int getDriveNumber() const {
      return _driveNumber;
   }
   FRESULT mount();
   std::expected<uint64_t, FRESULT> free();
   FRESULT remove(const char *fileName);
};
