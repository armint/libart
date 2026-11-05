// Compiled by tests/check-compat-firmware-build.sh for a target with and one without lock-free atomics.
// EXPECTED_RB_USE_STD_ATOMIC is the backend that utils/RingBuffer.h must select there.
#include <cstdint>
#include <utils/RingBuffer.h>

static_assert(RB_USE_STD_ATOMIC == EXPECTED_RB_USE_STD_ATOMIC, "RingBuffer.h selected the wrong backend");

RingBuffer<std::uint8_t, 32> byteRing;
RingBuffer<std::uint32_t, 131072> wordRing;

bool ringBufferRoundTrip() {
   std::uint8_t b = 0;
   std::uint32_t w = 0;
   return byteRing.try_push(1) && byteRing.try_pop(b) && wordRing.try_push(2) && wordRing.try_pop(w);
}
