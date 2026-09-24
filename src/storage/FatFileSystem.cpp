#include "FatFileSystem.h"
#include <array>
#include <cassert>
#include <cstring>

std::optional<FatFileSystem::Drive> FatFileSystem::devices[FF_VOLUMES];

FatFileSystem::Drive *FatFileSystem::drive(BYTE pdrv) {
   if(pdrv >= std::size(devices) || !devices[pdrv]) {
      return nullptr;
   }
   return &*devices[pdrv];
}

FatFileSystem::FatFileSystem(art::storage::BlockDevice &blockDevice) {
   _driveNumber = 0;
   for(auto &device : devices) {
      if(!device) {
         break;
      }
      _driveNumber++;
   }
   assert(_driveNumber < 10 && _driveNumber < std::size(devices));
   devices[_driveNumber].emplace(Drive{ &blockDevice, false });
}


FatFileSystem::~FatFileSystem() {
   char mountPoint[3]{};
   mountPoint[0] = static_cast<char>(_driveNumber + '0');
   mountPoint[1] = ':';
   f_unmount(mountPoint);
   devices[_driveNumber].reset();
}

FRESULT FatFileSystem::mount() {
   char mountPoint[3]{};
   if(_driveNumber > 9) {
      return FR_INVALID_DRIVE;
   }
   const char d = static_cast<char>(_driveNumber + '0');
   mountPoint[0] = d;
   mountPoint[1] = ':';
   return f_mount(&_fatfs, mountPoint, 0);
}
std::expected<uint64_t, FRESULT> FatFileSystem::free() {
   FATFS *fs = &_fatfs;
   DWORD freeCluster;
   uint64_t freeBytes = 0;
   char mountPoint[3]{};
   const char d = static_cast<char>(_driveNumber + '0');
   mountPoint[0] = d;
   mountPoint[1] = ':';
   auto res = f_getfree(mountPoint, &freeCluster, &fs);
   if(res != FR_OK) {
      return std::unexpected(res);
   }
   // FF_MIN_SS/ssize is the FAT sector size FatFs itself works with (see diskio.cpp).
#if FF_MAX_SS != FF_MIN_SS
   const uint32_t sectorSize = fs->ssize;
#else
   const uint32_t sectorSize = FF_MIN_SS;
#endif
   freeBytes = static_cast<uint64_t>(freeCluster) * fs->csize * sectorSize;

   return freeBytes;
}

FRESULT FatFileSystem::remove(const char *fileName) {
   if(!fileName) {
      return FR_INVALID_NAME;
   }
   char fullPath[FF_LFN_BUF + 4]{};
   const char d = static_cast<char>(_driveNumber + '0');
   fullPath[0] = d;
   fullPath[1] = ':';
   strncpy(fullPath + 2, fileName, FF_LFN_BUF);
   fullPath[sizeof(fullPath) - 1] = '\0';

   return f_unlink(fullPath);
}
