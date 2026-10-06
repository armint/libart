// Smoke test of the installed art-core package (see CMakeLists.txt and tests/check-install.sh): compiles against the
// installed headers, uses an ETL type through art::span and links functions of libart-core.a. With the own types,
// art::span must be the own span of the installed span-compat package.
#include "streams/ArrayWritable.h"
#include "utils/FloatText.h"
#include "utils/compat/expected.h"
#include "utils/compat/span.h"

#include <etl/vector.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>

static_assert(ART_USE_STD_EXPECTED_SPAN == ART_CONSUMER_EXPECT_STD_TYPES,
              "ART_USE_STD_EXPECTED_SPAN of the installed art::core is not the expected one");
#if ART_USE_STD_EXPECTED_SPAN
static_assert(__cplusplus > 202002L, "art::core with the std types must raise the consumer to C++23");
#else
static_assert(SPAN_COMPAT_USE_STD_SPAN == 0, "art::core with the own types must give the consumer the own span");
static_assert(std::is_same_v<art::span<uint8_t>, span_compat::span<uint8_t>>);
#endif

int main() {
   std::array<uint8_t, 16> buffer{};
   ArrayWritable writable{ art::span<uint8_t>(buffer) };
   const uint8_t data[] = { 'a', 'r', 't' };
   const auto written = writable.write(art::span<const uint8_t>(data));
   if(!written || *written != sizeof(data) || writable.position() != sizeof(data)) {
      std::puts("ArrayWritable::write failed");
      return 1;
   }

   etl::vector<char, utils::FloatText::kMaxTextLength> text(utils::FloatText::kMaxTextLength);
   const size_t length = utils::FloatText::formatFloat(1.5F, art::span<char>(text.data(), text.size()));
   if(length != 3 || std::memcmp(text.data(), "1.5", 3) != 0) {
      std::puts("formatFloat failed");
      return 1;
   }
   const auto parsed = utils::FloatText::parseFloat(std::string_view(text.data(), length));
   if(!parsed || *parsed != 1.5F) {
      std::puts("parseFloat failed");
      return 1;
   }

   std::printf("art-core install consumer: OK (ART_USE_STD_EXPECTED_SPAN=%d, __cplusplus=%ld)\n",
               ART_USE_STD_EXPECTED_SPAN, static_cast<long>(__cplusplus));
   return 0;
}
