
#pragma once
#include "streams/RandomAccessReadable.h"


#include "storage/FatFile.h"

#include "streams/BoundedReadable.h"

/**
 * A readable to read from a file in the FAT filesystem
 */
class FatFileReadable : public RandomAccessReadable {
   FatFile &file;
   std::span<uint8_t> _buffer;
   size_t bufferIndex = 0;
   size_t bytesBuffered = 0;
   FRESULT _lastError = FR_OK;

   public:
   FatFileReadable(FatFile &file, std::span<uint8_t> buffer)
      : file(file)
      , _buffer(buffer) {}

   virtual std::expected<char, Error> read() override;

   virtual bool available() override {
      return bufferIndex < bytesBuffered || file.tell() < file.size();
   }

   virtual size_t bytesLeft() override {
      size_t leftInFile = 0;
      size_t fileSize = file.size();
      size_t currentPosition = file.tell();
      if(fileSize > currentPosition) {
         leftInFile = fileSize - currentPosition;
      }
      return (bytesBuffered - bufferIndex) + leftInFile;
   }
   virtual void reset(size_t position = 0) override;

   virtual size_t position() override {
      return file.tell() - bytesBuffered + bufferIndex;
   }

   FRESULT lastError() const {
      return _lastError;
   }
};
