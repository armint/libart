// EXPECT: malformedDescriptorSequence
#include "usb/DescriptorBuilder.h"

// bLength 1 is not a descriptor.
constexpr std::array<uint8_t, 3> kBroken{ 0x01, 0x10, 0x02 };
constexpr auto kBos = art::usb::descriptor::bos(kBroken);
