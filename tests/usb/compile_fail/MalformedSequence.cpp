// EXPECT: malformedDescriptorSequence
// A hand-written descriptor with bLength 0.
#include "usb/DescriptorBuilder.h"

using namespace art::usb::descriptor;

constexpr std::array<uint8_t, 3> kBroken{ 0u, 4u, 0u };
constexpr auto kConfiguration = configuration(ConfigurationFields{}, kBroken);
