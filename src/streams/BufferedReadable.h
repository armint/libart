#pragma once

#include "Readable.h"

class BufferedReadable : public Readable {
   public:
   /**
    * Empty the buffer
    */
   virtual void clear() = 0;
};