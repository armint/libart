#pragma once
#include "BoundedReadable.h"


class BoundedReadableWrapper : public BoundedReadable {
   protected:
   const size_t limit_;
   size_t position_ = 0;
   Readable &source_;

   public:
   BoundedReadableWrapper(Readable &source_, size_t limit)
      : limit_(limit)
      , source_(source_) {}

   using Readable::read;
   std::expected<char, Error> read() override;
   bool available() override;
   size_t bytesLeft() override;
   size_t position() override;
};