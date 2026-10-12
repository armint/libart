// EXPECT: malformedDescriptorSequence
#include "usb/MsOs20.h"

// A part whose wLength (3) is below the 4 bytes of a descriptor header.
constexpr std::array<uint8_t, 4> kBroken{ 0x03, 0x00, 0x03, 0x00 };
constexpr auto kSet = art::usb::msos20::descriptorSet(art::usb::msos20::kWindows81, kBroken);
