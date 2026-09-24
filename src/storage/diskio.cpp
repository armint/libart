#include <limits>
#include <span>

#include "FatFileSystem.h"

using art::storage::Error;
using art::storage::Geometry;
using art::storage::offset_t;

// The one place that knows the FAT sector size: FatFs addresses the medium in sectors, a BlockDevice in bytes.
// GET_SECTOR_SIZE is not implemented, so the sector size must be fixed.
static_assert(FF_MIN_SS == FF_MAX_SS, "diskio.cpp supports a fixed FAT sector size only");

namespace {

constexpr uint32_t kSectorSize = FF_MIN_SS;

// Maps a device error onto the closest DRESULT. FatFs derives FR_NOT_READY from RES_NOTRDY, FR_WRITE_PROTECTED
// from RES_WRPRT, FR_INVALID_PARAMETER from RES_PARERR and FR_DISK_ERR from RES_ERROR. Timeout and Busy have no
// DRESULT of their own and count as a failed access, like FR_DISK_ERR.
DRESULT toDresult(Error error) {
   if(error == Error::NotReady || error == Error::NotInitialised) {
      return RES_NOTRDY;
   }
   if(error == Error::WriteProtected) {
      return RES_WRPRT;
   }
   if(error == Error::InvalidArgument) {
      return RES_PARERR;
   }
   return RES_ERROR;
}

// Maps the outcome of a device call and, for a device that says it is gone (no medium, not initialised),
// clears `initialised`. FatFs then runs disk_initialize() again on the next f_mount(), the way it does for a
// removable medium.
DRESULT finish(const std::expected<void, Error> &result, bool &initialised) {
   if(result) {
      return RES_OK;
   }
   const DRESULT dresult = toDresult(result.error());
   if(dresult == RES_NOTRDY) {
      initialised = false;
   }
   return dresult;
}

// The device can serve FAT sectors if a sector is a whole number of its read and program units, and if FatFs can
// address its capacity (LBA_t is 32 bits wide, FF_LBA64 is off).
bool supportsFatSectors(const Geometry &geometry) {
   return geometry.isValid() && kSectorSize % geometry.readSize == 0u && kSectorSize % geometry.programSize == 0u &&
          geometry.size >= kSectorSize && geometry.size / kSectorSize <= std::numeric_limits<LBA_t>::max();
}

// Byte length of `count` sectors, or 0 if it does not fit a size_t.
size_t sectorBytes(UINT count) {
   const uint64_t bytes = static_cast<uint64_t>(count) * kSectorSize;
   return bytes <= std::numeric_limits<size_t>::max() ? static_cast<size_t>(bytes) : 0u;
}

offset_t sectorOffset(LBA_t sector) {
   return static_cast<offset_t>(sector) * kSectorSize;
}

} // namespace

DSTATUS disk_initialize(BYTE pdrv) {
   auto *drive = FatFileSystem::drive(pdrv);
   if(!drive) {
      return STA_NOINIT;
   }
   drive->initialised = drive->device->init().has_value() && supportsFatSectors(drive->device->geometry());
   return drive->initialised ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
   const auto *drive = FatFileSystem::drive(pdrv);
   return drive && drive->initialised ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
   auto *drive = FatFileSystem::drive(pdrv);
   const size_t bytes = sectorBytes(count);
   if(!drive || !buff || !count || !bytes) {
      return RES_PARERR;
   }
   if(!drive->initialised) {
      return RES_NOTRDY;
   }
   const std::span<uint8_t> data(buff, bytes);
   return finish(drive->device->read(sectorOffset(sector), data), drive->initialised);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
   auto *drive = FatFileSystem::drive(pdrv);
   const size_t bytes = sectorBytes(count);
   if(!drive || !buff || !count || !bytes) {
      return RES_PARERR;
   }
   if(!drive->initialised) {
      return RES_NOTRDY;
   }
   const std::span<const uint8_t> data(buff, bytes);
   return finish(drive->device->program(sectorOffset(sector), data), drive->initialised);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
   auto *drive = FatFileSystem::drive(pdrv);
   if(!drive) {
      return RES_PARERR;
   }
   if(!drive->initialised) {
      return RES_NOTRDY; /* Check if drive is ready */
   }
   const Geometry geometry = drive->device->geometry();

   switch(cmd) {
   case CTRL_SYNC: /* Wait for end of internal write process of the drive */
      return finish(drive->device->sync(), drive->initialised);

   case GET_SECTOR_COUNT: /* Get drive capacity in unit of sector (DWORD) */
      *static_cast<LBA_t *>(buff) = static_cast<LBA_t>(geometry.size / kSectorSize);
      return RES_OK;

   case GET_BLOCK_SIZE: /* Get erase block size in unit of sector (DWORD) */
      // ff.c wants sectors (it aligns the data area to this many sectors, ignoring a value that is 0, not a
      // power of two or above 0x8000). A device without erase unit reports 0, which ff.c treats as unknown.
      *static_cast<DWORD *>(buff) = geometry.eraseSize / kSectorSize;
      return RES_OK;

   case CTRL_TRIM: /* Inform the device that a range of sectors is no longer used (FF_USE_TRIM == 1) */ {
      // args[0]/args[1] are the inclusive start/end sector of the range (see ff.c's callers). The hint may only
      // erase whole erase units, so the range shrinks to the units inside it. A device without an erase unit has
      // nothing to trim.
      const auto *args = static_cast<const LBA_t *>(buff);
      const offset_t unit = geometry.eraseSize;
      if(unit == 0u) {
         return RES_OK;
      }
      const offset_t first = (sectorOffset(args[0]) + unit - 1u) / unit * unit;
      const offset_t end = (sectorOffset(args[1]) + kSectorSize) / unit * unit;
      if(first >= end) {
         return RES_OK;
      }
      return finish(drive->device->erase(first, end - first), drive->initialised);
   }

   default:
      return RES_PARERR;
   }
}
