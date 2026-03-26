#include "FatDir.h"

FRESULT FatDir::read(FILINFO &info) {
   return FR_DENIED;
}

FatDir::FatDir(const char *path) {
   _res = f_opendir(&_dir, path);
}

FRESULT FatDir::seek(size_t offset) {
   size_t dptr = static_cast<size_t>(_dir.dptr);

   if(offset < dptr) {
      f_rewinddir(&_dir);
      dptr = 0;
   }
   while(dptr < offset) {
      FILINFO finfo;
      FRESULT res;

      res = f_readdir(&_dir, &finfo);
      if(res != FR_OK) {
         return res;
      } else if(finfo.fname[0] == 0) {
         return FR_NO_PATH;
      }
      dptr = static_cast<size_t>(_dir.dptr);
   }
   return FR_OK;
}

size_t FatDir::tell() {
   return _dir.dptr;
}
