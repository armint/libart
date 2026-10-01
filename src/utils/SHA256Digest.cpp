#include "SHA256Digest.h"

namespace {
// SHA-256 logical functions as defined in FIPS 180-4, section 4.1.2.
// Implemented as inline functions for type safety and performance.

inline constexpr uint32_t rotateRight(uint32_t word, uint32_t bits) {
   return (word >> bits) | (word << (32 - bits));
}

inline constexpr uint32_t choose(uint32_t x, uint32_t y, uint32_t z) {
   return (x & y) ^ (~x & z);
}

inline constexpr uint32_t majority(uint32_t x, uint32_t y, uint32_t z) {
   return (x & y) ^ (x & z) ^ (y & z);
}

inline constexpr uint32_t sigma0(uint32_t x) {
   return rotateRight(x, 2) ^ rotateRight(x, 13) ^ rotateRight(x, 22);
}

inline constexpr uint32_t sigma1(uint32_t x) {
   return rotateRight(x, 6) ^ rotateRight(x, 11) ^ rotateRight(x, 25);
}

inline constexpr uint32_t smallSigma0(uint32_t x) {
   return rotateRight(x, 7) ^ rotateRight(x, 18) ^ (x >> 3);
}

inline constexpr uint32_t smallSigma1(uint32_t x) {
   return rotateRight(x, 17) ^ rotateRight(x, 19) ^ (x >> 10);
}

// Initial hash values and round constants are from FIPS 180-4, sections 5.3.3 and 4.2.2.
constexpr uint32_t ROUND_CONSTANTS[64] = {
   0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
   0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
   0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
   0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
   0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
   0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
   0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
   0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
} // namespace

SHA256Digest::SHA256Digest() {
   reset();
}

void SHA256Digest::reset() {
   // Initial hash values (first 32 bits of the fractional parts of the square roots of the first 8 primes).
   intermediateHash_[0] = 0x6a09e667;
   intermediateHash_[1] = 0xbb67ae85;
   intermediateHash_[2] = 0x3c6ef372;
   intermediateHash_[3] = 0xa54ff53a;
   intermediateHash_[4] = 0x510e527f;
   intermediateHash_[5] = 0x9b05688c;
   intermediateHash_[6] = 0x1f83d9ab;
   intermediateHash_[7] = 0x5be0cd19;
   totalBitLength_ = 0;
   bufferIndex_ = 0;
   isFinalized_ = false;
}

void SHA256Digest::processBlock(art::span<const uint8_t, 64> data) {
   uint32_t workingA, workingB, workingC, workingD, workingE, workingF, workingG, workingH, temp1, temp2;

   /*
    * Memory Optimization: Circular Message Schedule Buffer.
    * Standard SHA-256 uses 64 words (256 bytes) for the message schedule (W).
    * We use only 16 words (64 bytes) and treat it as a circular buffer.
    * Since each W[i] is only needed to compute W[i+16], we can reuse the space.
    */
   uint32_t messageSchedule[16];

   // Initialize the first 16 words of the message schedule from the data block (big-endian).
   for(int i = 0; i < 16; ++i) {
      messageSchedule[i] = static_cast<uint32_t>(data[i * 4]) << 24 | static_cast<uint32_t>(data[i * 4 + 1]) << 16 |
                           static_cast<uint32_t>(data[i * 4 + 2]) << 8 | static_cast<uint32_t>(data[i * 4 + 3]);
   }

   // Initialize working variables with current intermediate hash value.
   workingA = intermediateHash_[0];
   workingB = intermediateHash_[1];
   workingC = intermediateHash_[2];
   workingD = intermediateHash_[3];
   workingE = intermediateHash_[4];
   workingF = intermediateHash_[5];
   workingG = intermediateHash_[6];
   workingH = intermediateHash_[7];

   // Main transformation loop (64 rounds).
   for(int i = 0; i < 64; ++i) {
      if(i >= 16) {
         // Extend the message schedule using the circular buffer.
         // W[i] = smallSigma1(W[i-2]) + W[i-7] + smallSigma0(W[i-15]) + W[i-16]
         messageSchedule[i & 15] = smallSigma1(messageSchedule[(i - 2) & 15]) + messageSchedule[(i - 7) & 15] +
                                   smallSigma0(messageSchedule[(i - 15) & 15]) + messageSchedule[i & 15];
      }

      temp1 = workingH + sigma1(workingE) + choose(workingE, workingF, workingG) + ROUND_CONSTANTS[i] + messageSchedule[i & 15];
      temp2 = sigma0(workingA) + majority(workingA, workingB, workingC);

      workingH = workingG;
      workingG = workingF;
      workingF = workingE;
      workingE = workingD + temp1;
      workingD = workingC;
      workingC = workingB;
      workingB = workingA;
      workingA = temp1 + temp2;
   }

   // Update intermediate hash with the results of the rounds.
   intermediateHash_[0] += workingA;
   intermediateHash_[1] += workingB;
   intermediateHash_[2] += workingC;
   intermediateHash_[3] += workingD;
   intermediateHash_[4] += workingE;
   intermediateHash_[5] += workingF;
   intermediateHash_[6] += workingG;
   intermediateHash_[7] += workingH;
}

void SHA256Digest::update(art::span<const uint8_t> data) {
   if(isFinalized_) {
      return;
   }
   for(auto byte : data) {
      dataBlock_[bufferIndex_++] = byte;
      if(bufferIndex_ == 64) {
         processBlock(art::span<const uint8_t, 64>(dataBlock_));
         totalBitLength_ += 512;
         bufferIndex_ = 0;
      }
   }
}

void SHA256Digest::finalize() {
   if(isFinalized_) {
      return;
   }

   uint32_t currentOffset = bufferIndex_;
   totalBitLength_ += currentOffset * 8;

   // Append the '1' bit followed by '0' bits (padding).
   dataBlock_[currentOffset++] = 0x80;

   // If there is no space for the 64-bit length (8 bytes), process this block and start a new one.
   if(currentOffset > 56) {
      while(currentOffset < 64) {
         dataBlock_[currentOffset++] = 0x00;
      }
      processBlock(art::span<const uint8_t, 64>(dataBlock_));
      currentOffset = 0;
   }

   // Fill remaining space with zeros until the length field.
   while(currentOffset < 56) {
      dataBlock_[currentOffset++] = 0x00;
   }

   // Append the total message length in bits (big-endian).
   dataBlock_[56] = static_cast<uint8_t>(totalBitLength_ >> 56);
   dataBlock_[57] = static_cast<uint8_t>(totalBitLength_ >> 48);
   dataBlock_[58] = static_cast<uint8_t>(totalBitLength_ >> 40);
   dataBlock_[59] = static_cast<uint8_t>(totalBitLength_ >> 32);
   dataBlock_[60] = static_cast<uint8_t>(totalBitLength_ >> 24);
   dataBlock_[61] = static_cast<uint8_t>(totalBitLength_ >> 16);
   dataBlock_[62] = static_cast<uint8_t>(totalBitLength_ >> 8);
   dataBlock_[63] = static_cast<uint8_t>(totalBitLength_);

   processBlock(art::span<const uint8_t, 64>(dataBlock_));
   isFinalized_ = true;
}

art::expected<size_t, Digest::Error> SHA256Digest::getDigest(art::span<uint8_t> target) {
   if(target.size() < 32) {
      return art::unexpected(ArrayTooSmall);
   }
   finalize();
   // Copy intermediate hash to target buffer (big-endian).
   for(int i = 0; i < 8; ++i) {
      target[i * 4] = static_cast<uint8_t>(intermediateHash_[i] >> 24);
      target[i * 4 + 1] = static_cast<uint8_t>(intermediateHash_[i] >> 16);
      target[i * 4 + 2] = static_cast<uint8_t>(intermediateHash_[i] >> 8);
      target[i * 4 + 3] = static_cast<uint8_t>(intermediateHash_[i]);
   }
   return 32;
}
