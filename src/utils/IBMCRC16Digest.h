
#pragma once
#include "Digest.h"


class IBMCRC16Digest : public Digest {
   protected:
   const uint16_t polynomial_;
   const uint16_t initial_value_;
   uint16_t digest_;

   public:
   IBMCRC16Digest(uint16_t polynomial = 0xA001, uint16_t initialValue = 0xFFFF)
      : polynomial_(polynomial)
      , initial_value_(initialValue)
      , digest_(initialValue) {}

   void update(art::span<const uint8_t> data) override;
   art::expected<size_t, Error> getDigest(art::span<uint8_t> target) override;
   void reset() override;
};
