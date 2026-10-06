// Smoke test of libArT in a superproject (see CMakeLists.txt and tests/check-subproject.sh).
#include "streams/ArrayWritable.h"
#include "utils/compat/span.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#if ART_CONSUMER_HAL
#include "hal.h"

static_assert(std::is_same_v<art::span<const uint8_t>, span_compat::span<const uint8_t>>,
              "libArT and the span-compat library must use one span type");
#endif

int main() {
   std::array<uint8_t, 4> buffer{};
   ArrayWritable writable{ art::span<uint8_t>(buffer) };
   const uint8_t data[] = { 1, 2, 3 };
   const auto written = writable.write(art::span<const uint8_t>(data));
   if(!written || *written != sizeof(data)) {
      std::puts("ArrayWritable::write failed");
      return 1;
   }
#if ART_CONSUMER_HAL
   if(halChecksum(art::span<const uint8_t>(buffer).first(sizeof(data))) != 6) {
      std::puts("halChecksum failed");
      return 1;
   }
#endif
   std::printf("art-core subproject consumer: OK (ART_USE_STD_EXPECTED_SPAN=%d, __cplusplus=%ld)\n",
               ART_USE_STD_EXPECTED_SPAN, static_cast<long>(__cplusplus));
   return 0;
}
