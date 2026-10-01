
#include "FatFile.h"
#ifdef UNIT_TESTING
#include <algorithm>
#endif

FatFile::FatFile(const char *path, uint8_t mode) {
#ifdef UNIT_TESTING
   if(state) {
      _res = FR_OK;
      return;
   }
#endif
   _res = f_open(&_fil, path, mode);
}

FatFile::~FatFile() {
#ifdef UNIT_TESTING
   if(state)
      return;
#endif
   if(_res == FR_OK) {
      f_close(&_fil);
   }
}

art::expected<size_t, FRESULT> FatFile::read(art::span<uint8_t> buffer) {
#ifdef UNIT_TESTING
   if(state) {
      if(state->nextReadResult != FR_OK) {
         auto res = state->nextReadResult;
         state->nextReadResult = FR_OK;
         return art::unexpected(res);
      }
      size_t toRead = std::min(buffer.size(), state->fileData.size() - state->currentPosition);
      std::copy(state->fileData.begin() + state->currentPosition,
                state->fileData.begin() + state->currentPosition + toRead, buffer.begin());
      state->currentPosition += toRead;
      return toRead;
   }
#endif
   UINT read;
   auto res = f_read(&_fil, buffer.data(), buffer.size(), &read);
   if(res != FR_OK) {
      return art::unexpected(res);
   }
   return static_cast<size_t>(read);
}

art::expected<size_t, FRESULT> FatFile::write(art::span<const uint8_t> buffer) {
#ifdef UNIT_TESTING
   if(state) {
      state->writeCalled = true;
      state->writtenData.insert(state->writtenData.end(), buffer.begin(), buffer.end());
      state->currentPosition += buffer.size();
      return buffer.size();
   }
#endif
   UINT written;
   auto res = f_write(&_fil, buffer.data(), buffer.size(), &written);
   if(res != FR_OK) {
      return art::unexpected(res);
   }
   return static_cast<size_t>(written);
}

art::expected<void, FRESULT> FatFile::seek(size_t offset) {
#ifdef UNIT_TESTING
   if(state) {
      state->currentPosition = offset;
      return {};
   }
#endif
   auto res = f_lseek(&_fil, offset);
   if(res != FR_OK) {
      return art::unexpected(res);
   }
   return {};
}

size_t FatFile::tell() {
#ifdef UNIT_TESTING
   if(state)
      return state->currentPosition;
#endif
   return f_tell(&_fil);
}

size_t FatFile::size() {
#ifdef UNIT_TESTING
   if(state)
      return state->fileData.size();
#endif
   return f_size(&_fil);
}

art::expected<void, FRESULT> FatFile::flush() {
#ifdef UNIT_TESTING
   if(state) {
      state->syncCalled = true;
      state->flushed = true;
      return {};
   }
#endif
   auto res = f_sync(&_fil);
   if(res != FR_OK) {
      return art::unexpected(res);
   }
   return {};
}
