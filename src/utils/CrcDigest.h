// CrcDigest.h
#pragma once

#include "Digest.h"
#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

/**
 * Generic CRC digest implementation with compile-time configuration.
 *
 * Template parameters:
 *   - Width: bit width (8, 16, or 32)
 *   - Polynomial: CRC polynomial (e.g., 0x07 for CRC-8, 0x1021 for CRC-16-CCITT)
 *   - Init: initial CRC value (e.g., 0x00, 0xFF, 0xFFFF)
 *   - ReflectIn: reflect input bytes (true for CRC-16/CCITT)
 *   - ReflectOut: reflect final CRC (true for CRC-16/CCITT)
 *   - XorOut: final XOR mask (e.g., 0x0000, 0xFFFF)
 */

template <std::uint8_t Width,
          std::uintmax_t Polynomial,
          std::uintmax_t Init = 0,
          bool ReflectIn = false,
          bool ReflectOut = false,
          std::uintmax_t XorOut = 0>
class CrcDigest : public Digest {
   static_assert(Width == 8 || Width == 16 || Width == 32, "Width must be 8, 16, or 32");
   static_assert(Polynomial < (std::uintmax_t{1} << Width), "Polynomial must fit in Width bits");

   using CrcType = typename std::conditional<
      Width <= 8,  std::uint8_t,
      typename std::conditional<Width <= 16, std::uint16_t, std::uint32_t>::type
   >::type;

   static constexpr CrcType MASK = (CrcType{1} << Width) - 1;
   static constexpr CrcType POLY = static_cast<CrcType>(Polynomial);
   static constexpr CrcType INIT_VAL = static_cast<CrcType>(Init);

   // === Helper declarations (must be BEFORE any usage in constexpr) ===
   static constexpr std::uint8_t reflect8(std::uint8_t x) {
      std::uint8_t r = 0;
      for (int i = 0; i < 8; ++i) {
         r = (r << 1) | (x & 1);
         x >>= 1;
      }
      return r;
   }

   static constexpr CrcType reflectCrc(CrcType x, std::uint8_t n) {
      CrcType r = 0;
      for (std::uint8_t i = 0; i < n; ++i) {
         if (x & 1) {
            r |= CrcType{1} << (n - 1 - i);
         }
         x >>= 1;
      }
      return r;
   }

   // === Table generation: only used for MSB mode ===
   static constexpr auto makeTable() {
      std::array<CrcType, 256> table{};
      if constexpr (!ReflectIn) {
         for (std::uint16_t i = 0; i < 256; ++i) {
            CrcType crc = static_cast<CrcType>(i) << (Width - 8);
            for (std::uint8_t bit = 0; bit < 8; ++bit) {
               if (crc & (CrcType{1} << (Width - 1))) {
                  crc = (crc << 1) ^ POLY;
               } else {
                  crc <<= 1;
               }
            }
            table[i] = crc & MASK;
         }
      }
      return table;
   }

   static constexpr std::array<CrcType, 256> TABLE = makeTable();

   // === Finalize helper ===
   CrcType finalize(CrcType crc) const {
      if constexpr (ReflectOut) {
         crc = reflectCrc(crc, Width);
      }
      return (crc ^ static_cast<CrcType>(XorOut)) & MASK;
   }

 public:
   CrcDigest() : crc_(INIT_VAL) {}

   void update(std::span<const uint8_t> data) override {
      for (uint8_t byte : data) {
         if constexpr (ReflectIn) {
            // LSB-first CRC update — POLY is *already reflected* (e.g., 0x8005)
            crc_ ^= static_cast<CrcType>(byte);
            for (std::uint8_t bit = 0; bit < 8; ++bit) {
               if (crc_ & 1) {
                  crc_ = (crc_ >> 1) ^ POLY;
               } else {
                  crc_ >>= 1;
               }
            }
         } else {
            // MSB-first: original logic
            std::uint8_t idx = static_cast<std::uint8_t>(byte ^ (crc_ >> (Width - 8)));
            crc_ = (static_cast<CrcType>(crc_) << 8) ^ TABLE[idx];
            crc_ &= MASK;
         }
      }
   }

   std::expected<size_t, Error> getDigest(std::span<uint8_t> target) override {
      if (target.size() < (Width + 7) / 8) {
         return std::unexpected(ArrayTooSmall);
      }

      CrcType finalCrc = finalize(crc_);

      // Little-endian output
      if constexpr (Width == 8) {
         target[0] = static_cast<uint8_t>(finalCrc);
      } else if constexpr (Width == 16) {
         target[0] = static_cast<uint8_t>(finalCrc & 0xFF);
         target[1] = static_cast<uint8_t>((finalCrc >> 8) & 0xFF);
      } else { // 32-bit
         target[0] = static_cast<uint8_t>(finalCrc & 0xFF);
         target[1] = static_cast<uint8_t>((finalCrc >> 8) & 0xFF);
         target[2] = static_cast<uint8_t>((finalCrc >> 16) & 0xFF);
         target[3] = static_cast<uint8_t>((finalCrc >> 24) & 0xFF);
      }

      return (Width + 7) / 8;
   }

   void reset() override {
      crc_ = INIT_VAL;
   }

   [[nodiscard]] CrcType value() const { return finalize(crc_); }

 private:
   CrcType crc_;
};

// ✅ Correct Modbus definition: reflected poly (0x8005), LSB mode
using TmcCrc8 = CrcDigest<8, 0x07, 0x00, false, false, 0x00>;
using Crc16Ccitt = CrcDigest<16, 0x1021, 0xFFFF, true, true, 0x0000>;
using ModbusCrc = CrcDigest<16, 0xA001, 0xFFFF, true, false, 0x0000>;
