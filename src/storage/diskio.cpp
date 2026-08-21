#include <array>

#include "FatFileSystem.h"


DSTATUS disk_initialize(BYTE pdrv) {
   if(pdrv >= std::size(FatFileSystem::devices)) {
      return STA_NOINIT;
   }
   if(!FatFileSystem::devices[pdrv]) {
      return STA_NOINIT;
   }
   return (*FatFileSystem::devices[pdrv])->initialize();
}

DSTATUS disk_status(BYTE pdrv) {
   if(pdrv >= std::size(FatFileSystem::devices) || !FatFileSystem::devices[pdrv]) {
      return STA_NOINIT;
   }
   return (*FatFileSystem::devices[pdrv])->getStatus();
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
   if(pdrv >= std::size(FatFileSystem::devices) || !FatFileSystem::devices[pdrv] || !count) {
      return RES_PARERR;
   }
   // StorageDevice::read() returns the number of sectors actually read (0 on
   // error), not a DRESULT code — a previous direct cast happened to work
   // out to RES_OK only when count == 0, misreporting every successful
   // single-sector read as RES_ERROR (RES_ERROR == 1 == a 1-sector count).
   return (*FatFileSystem::devices[pdrv])->read(buff, sector, count) == count ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
   if(pdrv >= std::size(FatFileSystem::devices) || !FatFileSystem::devices[pdrv] || !count) {
      return RES_PARERR;
   }
   // See disk_read() above: write() returns a sector count, not a DRESULT.
   return (*FatFileSystem::devices[pdrv])->write(buff, sector, count) == count ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
   DRESULT res;

   if(pdrv >= std::size(FatFileSystem::devices) || !FatFileSystem::devices[pdrv].has_value()) {
      return RES_PARERR;
   }
   if(disk_status(pdrv) == STA_NOINIT)
      return RES_NOTRDY; /* Check if drive is ready */

   res = RES_ERROR;

   switch(cmd) {
   case CTRL_SYNC: /* Wait for end of internal write process of the drive */
      /* SPI writes are synchronous — disk_write already waits for the card to
         accept each block before returning, so there is nothing left to flush. */
      res = RES_OK;
      break;

   case GET_SECTOR_COUNT: /* Get drive capacity in unit of sector (DWORD) */ {
      LBA_t count = (*FatFileSystem::devices[pdrv])->getSectorCount();
      if(count != 0) {
         *reinterpret_cast<LBA_t *>(buff) = count;
         res = RES_OK;
      }
      break;
   }

   case GET_BLOCK_SIZE: /* Get erase block size in unit of sector (DWORD) */ {
      res = RES_OK;
      *reinterpret_cast<LBA_t *>(buff) = (*FatFileSystem::devices[pdrv])->getEraseSize();
   } break;

   case CTRL_TRIM: /* Erase a block of sectors (used when _USE_ERASE == 1) */ {
      LBA_t *args = reinterpret_cast<LBA_t *>(buff);
      if((*FatFileSystem::devices[pdrv])->erase(args[0], args[1]) == 0) {
         res = RES_OK;
      }
      break;
   }

   default:
      res = RES_PARERR;
   }
   return res;
}