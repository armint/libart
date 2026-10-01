#include "BlockDeviceContract.h"
#include "utils/compat/span.h"
#include <algorithm>
#include <doctest/doctest.h>
#include <limits>
#include <numeric>
#include <vector>

namespace art::storage::test {

namespace {

// The checked window is at least this many bytes, and at least two units of the geometry.
constexpr uint64_t kPreferredWindowBytes = 512u;
// Sanity limit for the window: it is what the buffers below are sized for.
constexpr uint64_t kMaxWindowBytes = 4u * 1024u * 1024u;
// Round trips are dense (every alignment) near the start and the end of each erase unit and skip the middle of
// large units, which keeps the run time bounded for 64 KiB NOR blocks.
constexpr uint64_t kDenseEdgeBytes = 512u;

constexpr uint64_t alignDown(uint64_t value, uint64_t granularity) {
   return value / granularity * granularity;
}

constexpr uint64_t alignUp(uint64_t value, uint64_t granularity) {
   return alignDown(value + granularity - 1u, granularity);
}

// Test data that is not constant, so shifted or duplicated writes are detected.
constexpr uint8_t patternByte(uint64_t index, uint8_t seed) {
   return static_cast<uint8_t>((index * 31u + seed * 17u + 7u) ^ (index >> 3u));
}

class ContractChecker {
   public:
   ContractChecker(BlockDevice &device, std::optional<uint8_t> erasedByte)
      : device_(device)
      , erasedByte_(erasedByte) {
   }

   void run() {
      REQUIRE(device_.init().has_value());
      setUpGeometry();
      checkProgramReadRoundTrip();
      checkErase();
      checkOverwrite();
      checkReinitKeepsData();
      checkRangeRejection();
      checkAlignmentRejection();
      CHECK(device_.sync().has_value());
   }

   private:
   // Everything below relies on the geometry being valid, so a violation ends the contract run. geometry() is
   // only defined after init(), hence it is read here and not earlier.
   void setUpGeometry() {
      geometry_ = device_.geometry();
      REQUIRE(geometry_.isValid());

      // granule_ is the unit in which the checks erase and read back: whole erase units, or on a device
      // without erase the smallest range that is both read and program aligned.
      const uint64_t readProgramUnit = std::lcm<uint64_t>(geometry_.readSize, geometry_.programSize);
      granule_ = geometry_.eraseSize != 0u ? geometry_.eraseSize : readProgramUnit;
      const uint64_t commonUnit = std::lcm<uint64_t>(readProgramUnit, granule_);
      window_ = alignDown(std::min(geometry_.size, std::max(kPreferredWindowBytes, 2u * commonUnit)), commonUnit);
      REQUIRE(window_ > 0u);
      REQUIRE_MESSAGE(window_ <= kMaxWindowBytes,
                      "geometry granularity too coarse for the contract suite: window of " << window_ << " bytes");
      // The test host allocates the working buffers; the devices under test need not.
      snapshot_.resize(window_);
      readback_.resize(window_);
      nextRead_.resize(window_);
      pattern_.resize(window_);
   }

   // A device without erase (eraseSize 0) has nothing to erase, so this does nothing there.
   void eraseRange(uint64_t offset, uint64_t length) {
      if(geometry_.eraseSize != 0u) {
         INFO("erase offset " << offset << " length " << length);
         REQUIRE(device_.erase(offset, length).has_value());
      }
   }

   void readRange(uint64_t offset, art::span<uint8_t> out) {
      INFO("read offset " << offset << " length " << out.size());
      REQUIRE(device_.read(offset, out).has_value());
   }

   void programRange(uint64_t offset, art::span<const uint8_t> data) {
      INFO("program offset " << offset << " length " << data.size());
      REQUIRE(device_.program(offset, data).has_value());
   }

   art::span<uint8_t> pattern(size_t length, uint64_t firstIndex, uint8_t seed) {
      for(size_t index = 0u; index < length; ++index) {
         pattern_[index] = patternByte(firstIndex + index, seed);
      }
      return art::span<uint8_t>{ pattern_.data(), length };
   }

   static bool sameBytes(art::span<const uint8_t> first, art::span<const uint8_t> second) {
      return std::ranges::equal(first, second);
   }

   bool allBytesEqual(art::span<const uint8_t> bytes, uint8_t value) const {
      return std::ranges::all_of(bytes, [value](uint8_t byte) { return byte == value; });
   }

   void checkProgramReadRoundTrip() {
      const uint64_t programSize = geometry_.programSize;
      for(uint64_t offset = 0u; offset < window_; offset += programSize) {
         const uint64_t inGranule = offset % granule_;
         const uint64_t untilGranuleEnd = granule_ - inGranule;
         const bool nearEdge = inGranule < kDenseEdgeBytes || untilGranuleEnd <= kDenseEdgeBytes + 3u * programSize;
         if(!nearEdge) {
            continue;
         }
         for(uint64_t units = 1u; units <= 3u && offset + units * programSize <= window_; ++units) {
            checkRoundTripAt(offset, units * programSize);
         }
      }
   }

   void checkRoundTripAt(uint64_t offset, uint64_t length) {
      INFO("round trip offset " << offset << " length " << length);
      const uint64_t regionStart = alignDown(offset, granule_);
      const uint64_t regionEnd = alignUp(offset + length, granule_);
      const art::span<uint8_t> before{ snapshot_.data(), regionEnd - regionStart };
      const art::span<uint8_t> after{ readback_.data(), regionEnd - regionStart };

      eraseRange(regionStart, regionEnd - regionStart);
      readRange(regionStart, before);
      const art::span<const uint8_t> data = pattern(length, offset, 1u);
      programRange(offset, data);
      readRange(regionStart, after);

      const size_t headSize = offset - regionStart;
      CHECK(sameBytes(after.first(headSize), before.first(headSize)));
      CHECK(sameBytes(after.subspan(headSize, length), data));
      const size_t tailStart = headSize + length;
      CHECK(sameBytes(after.subspan(tailStart), before.subspan(tailStart)));

      // Reads at every readSize alignment inside the programmed range.
      const uint64_t readSize = geometry_.readSize;
      if(offset % readSize != 0u || length % readSize != 0u) {
         return;
      }
      for(uint64_t position = 0u; position < length; position += readSize) {
         for(uint64_t units = 1u; units <= 2u && position + units * readSize <= length; ++units) {
            const art::span<uint8_t> partial{ readback_.data(), units * readSize };
            readRange(offset + position, partial);
            CHECK(sameBytes(partial, data.subspan(position, units * readSize)));
         }
      }
   }

   // Bytes programmed by the erase and overwrite checks: at least 16, so that matching the erased pattern by
   // chance is not a concern.
   uint64_t probeLength() const {
      const uint64_t length = alignUp(16u, geometry_.programSize);
      return geometry_.eraseSize != 0u ? std::min<uint64_t>(length, geometry_.eraseSize) : length;
   }

   void checkErase() {
      if(geometry_.eraseSize == 0u) {
         // No erase unit: an in-bounds call of any alignment must succeed and change nothing.
         const uint64_t length = probeLength();
         const art::span<const uint8_t> data = pattern(length, 0u, 2u);
         programRange(0u, data);
         CHECK(device_.erase(0u, granule_).has_value());
         CHECK(device_.erase(1u, 3u).has_value());
         CHECK(device_.erase(0u, 0u).has_value());
         const art::span<uint8_t> after{ readback_.data(), alignUp(length, granule_) };
         readRange(0u, after);
         CHECK(sameBytes(after.first(length), data));
         return;
      }

      const uint64_t eraseSize = geometry_.eraseSize;
      const uint64_t length = probeLength();
      const bool hasSecondUnit = window_ >= 2u * eraseSize;
      const art::span<uint8_t> unit{ readback_.data(), eraseSize };

      eraseRange(0u, window_);
      if(erasedByte_) {
         const art::span<uint8_t> all{ snapshot_.data(), window_ };
         readRange(0u, all);
         CHECK(allBytesEqual(all, *erasedByte_));
      }

      programRange(0u, pattern(length, 0u, 2u));
      if(hasSecondUnit) {
         programRange(eraseSize, pattern(length, eraseSize, 3u));
      }
      const art::span<uint8_t> secondCheck{ snapshot_.data(), length };
      if(hasSecondUnit) {
         readRange(eraseSize, secondCheck);
         CHECK(sameBytes(secondCheck, pattern(length, eraseSize, 3u)));
      }

      // Erase only the first unit.
      REQUIRE(device_.erase(0u, eraseSize).has_value());
      readRange(0u, unit);
      if(erasedByte_) {
         CHECK(allBytesEqual(unit, *erasedByte_));
      }
      CHECK_FALSE(sameBytes(unit.first(length), pattern(length, 0u, 2u)));
      // Erased content is stable: reading does not change it.
      const art::span<uint8_t> unitAgain{ nextRead_.data(), eraseSize };
      readRange(0u, unitAgain);
      CHECK(sameBytes(unit, unitAgain));
      if(hasSecondUnit) {
         readRange(eraseSize, secondCheck);
         CHECK(sameBytes(secondCheck, pattern(length, eraseSize, 3u)));
      }

      // Erase across several units in one call.
      REQUIRE(device_.erase(0u, window_).has_value());
      if(hasSecondUnit) {
         readRange(eraseSize, secondCheck);
         CHECK_FALSE(sameBytes(secondCheck, pattern(length, eraseSize, 3u)));
      }
      if(erasedByte_) {
         const art::span<uint8_t> all{ snapshot_.data(), window_ };
         readRange(0u, all);
         CHECK(allBytesEqual(all, *erasedByte_));
      }
   }

   void checkOverwrite() {
      const uint64_t length = probeLength();
      const art::span<uint8_t> region{ readback_.data(), alignUp(length, granule_) };

      eraseRange(0u, granule_);
      programRange(0u, pattern(length, 0u, 4u));
      if(!geometry_.eraseBeforeProgram) {
         // Claimed to be overwritable in place: programming again without erase must replace the data.
         programRange(0u, pattern(length, 0u, 5u));
         readRange(0u, region);
         CHECK(sameBytes(region.first(length), pattern(length, 0u, 5u)));
      }
      // Erase-then-program must work again after the range was programmed.
      eraseRange(0u, granule_);
      programRange(0u, pattern(length, 0u, 6u));
      readRange(0u, region);
      CHECK(sameBytes(region.first(length), pattern(length, 0u, 6u)));
   }

   void checkReinitKeepsData() {
      const uint64_t length = probeLength();
      const art::span<uint8_t> region{ readback_.data(), alignUp(length, granule_) };
      eraseRange(0u, granule_);
      programRange(0u, pattern(length, 0u, 7u));
      CHECK(device_.init().has_value());
      readRange(0u, region);
      CHECK(sameBytes(region.first(length), pattern(length, 0u, 7u)));
      CHECK(sameGeometry(device_.geometry(), geometry_));
   }

   static bool sameGeometry(const Geometry &first, const Geometry &second) {
      const bool sameGranularity = first.readSize == second.readSize && first.programSize == second.programSize;
      const bool sameErase = first.eraseSize == second.eraseSize;
      const bool sameMode = first.eraseBeforeProgram == second.eraseBeforeProgram;
      return sameGranularity && sameErase && sameMode && first.size == second.size;
   }

   bool readFails(uint64_t offset, uint64_t length) {
      INFO("read offset " << offset << " length " << length);
      return failedWith(device_.read(offset, art::span<uint8_t>{ readback_.data(), length }), Error::InvalidArgument);
   }

   bool programFails(uint64_t offset, uint64_t length) {
      INFO("program offset " << offset << " length " << length);
      return failedWith(device_.program(offset, pattern(length, 0u, 8u)), Error::InvalidArgument);
   }

   bool eraseFails(uint64_t offset, uint64_t length) {
      INFO("erase offset " << offset << " length " << length);
      return failedWith(device_.erase(offset, length), Error::InvalidArgument);
   }

   // Puts known data at the start of the device, so a rejected erase or program has something to damage.
   void writeKnownData() {
      eraseRange(0u, window_);
      programRange(0u, pattern(geometry_.programSize, 0u, 9u));
   }

   // A rejected program or erase must leave the medium as it was, also when the rejected range starts inside the
   // device: a device that works in chunks must validate the whole range before it writes the first chunk.
   void checkLongRejectedRangeChangesNothing() {
      const uint64_t span = alignDown(std::min(geometry_.size / 2u, window_ / 2u), granule_);
      if(span == 0u) {
         return;
      }
      const uint64_t start = geometry_.size - span;
      const art::span<uint8_t> before{ snapshot_.data(), span };
      const art::span<uint8_t> after{ nextRead_.data(), span };
      readRange(start, before);

      CHECK(programFails(start, span + geometry_.programSize));
      readRange(start, after);
      CHECK(sameBytes(before, after));

      CHECK(eraseFails(start, span + std::max<uint64_t>(geometry_.eraseSize, 1u)));
      readRange(start, after);
      CHECK(sameBytes(before, after));
   }

   void checkRangeRejection() {
      checkLongRejectedRangeChangesNothing();

      writeKnownData();
      const uint64_t regionSize = std::min(2u * granule_, window_);
      const art::span<uint8_t> before{ snapshot_.data(), regionSize };
      const art::span<uint8_t> after{ readback_.data(), regionSize };
      readRange(0u, before);

      constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
      const uint64_t size = geometry_.size;
      const uint64_t readSize = geometry_.readSize;
      const uint64_t programSize = geometry_.programSize;

      CHECK(readFails(size, readSize));
      CHECK(readFails(size + readSize, readSize));
      CHECK(readFails(size - readSize, 2u * readSize));
      CHECK(readFails(alignDown(kMax, readSize), readSize)); // offset + length wraps around

      CHECK(programFails(size, programSize));
      CHECK(programFails(size + programSize, programSize));
      CHECK(programFails(size - programSize, 2u * programSize));
      CHECK(programFails(alignDown(kMax, programSize), programSize));

      if(const uint64_t eraseSize = geometry_.eraseSize; eraseSize == 0u) {
         // No erase unit: only the range is checked.
         CHECK(eraseFails(size, 1u));
         CHECK(eraseFails(size + 1u, 1u));
         CHECK(eraseFails(size - 1u, 2u));
         CHECK(eraseFails(0u, size + 1u));
         CHECK(eraseFails(kMax, 1u));
         CHECK(eraseFails(1u, kMax)); // offset + length wraps around
      } else {
         CHECK(eraseFails(size, eraseSize));
         CHECK(eraseFails(size - eraseSize, 2u * eraseSize));
         CHECK(eraseFails(0u, size + eraseSize));
         CHECK(eraseFails(alignDown(kMax, eraseSize), eraseSize));
      }

      readRange(0u, after);
      CHECK(sameBytes(before, after));
   }

   void checkAlignmentRejection() {
      // Rejected calls must also leave the medium as it was. The data is not rewritten first: whatever the
      // range rejections above left behind is part of what is compared.
      const uint64_t regionSize = std::min(2u * granule_, window_);
      const art::span<uint8_t> before{ snapshot_.data(), regionSize };
      const art::span<uint8_t> after{ readback_.data(), regionSize };
      readRange(0u, before);

      const uint64_t readSize = geometry_.readSize;
      if(readSize > 1u) {
         CHECK(readFails(1u, readSize));
         CHECK(readFails(0u, readSize + 1u));
         CHECK(readFails(readSize - 1u, readSize));
      }
      const uint64_t programSize = geometry_.programSize;
      if(programSize > 1u) {
         CHECK(programFails(1u, programSize));
         CHECK(programFails(0u, programSize + 1u));
         CHECK(programFails(programSize - 1u, programSize));
      }
      const uint64_t eraseSize = geometry_.eraseSize;
      if(eraseSize > 1u) {
         CHECK(eraseFails(1u, eraseSize));
         CHECK(eraseFails(0u, eraseSize + 1u));
         CHECK(eraseFails(eraseSize - 1u, eraseSize));
      }

      readRange(0u, after);
      CHECK(sameBytes(before, after));
   }

   BlockDevice &device_;
   std::optional<uint8_t> erasedByte_;
   Geometry geometry_{};
   uint64_t granule_{ 1u };
   uint64_t window_{ 0u };
   // Working buffers, each window_ bytes long.
   std::vector<uint8_t> snapshot_;
   std::vector<uint8_t> readback_;
   std::vector<uint8_t> nextRead_;
   std::vector<uint8_t> pattern_;
};

} // namespace

void checkBlockDeviceContract(BlockDevice &device, std::optional<uint8_t> erasedByte) {
   ContractChecker checker{ device, erasedByte };
   checker.run();
}

} // namespace art::storage::test
