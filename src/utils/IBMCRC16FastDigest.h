
#pragma once
#include "Digest.h"

class IBMCRC16FastDigest : public Digest {
   protected:
   const uint16_t initial_value_;
   uint16_t digest_;

   public:
   IBMCRC16FastDigest(uint16_t initialValue = 0xFFFF)
      : initial_value_(initialValue)
      , digest_(initialValue) {}

   void update(std::span<const uint8_t> data) override;
   std::expected<size_t, Error> getDigest(std::span<uint8_t> target) override;
   void reset() override {
      digest_ = initial_value_;
   }
};
