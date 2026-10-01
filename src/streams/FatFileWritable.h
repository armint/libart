
#pragma once

#include "storage/FatFile.h"
#include "streams/BoundedWritable.h"
#include "streams/BufferedWritable.h"
#include <limits>

/**
 * A BoundedWritable that writes to a file on the FAT filesystem
 */
class FatFileWritable : public BoundedWritable {
   art::span<uint8_t> buffer_;
   FatFile &file_;
   size_t index_ = 0;

   public:
   FatFileWritable(FatFile &file, art::span<uint8_t> buffer)
      : BoundedWritable(std::numeric_limits<uint32_t>::max())
      , buffer_(buffer)
      , file_(file) {
   }

   virtual ~FatFileWritable() override;
   virtual art::expected<size_t, Error> write(art::span<const uint8_t> data) override;
   virtual art::expected<void, Error> flush() override;
   virtual void reset(size_t position = 0) override;
   virtual size_t position() override;
};
