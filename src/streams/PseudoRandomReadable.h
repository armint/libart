
#pragma once

#include "Readable.h"
#include <utils/pch.h>

class PseudoRandomReadable : public Readable {
   protected:
   pcg::pcg32 rng_;
   uint32_t last_ = 0;
   uint8_t idx_ = 0;
   public:
   PseudoRandomReadable(uint64_t seed);
   art::expected<char, Error> read() override;
   bool available() override;
};
