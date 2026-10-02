#include "FatDir.h"

FRESULT FatDir::read(FILINFO &info) {
   // _dir holds no valid directory if opening failed
   if(_res != FR_OK) {
      return _res;
   }
   return f_readdir(&_dir, &info);
}

FatDir::FatDir(const char *path) {
   _res = f_opendir(&_dir, path);
}

FatDir::~FatDir() {
   if(_res == FR_OK) {
      f_closedir(&_dir);
   }
}

FRESULT FatDir::seek(size_t offset) {
   // _dir holds no valid directory if opening failed
   if(_res != FR_OK) {
      return _res;
   }
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
