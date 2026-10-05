// EXPECT: stringDescriptorNeedsNullTerminatedLiteral
#include "usb/DescriptorBuilder.h"

using namespace art::usb::descriptor;

constexpr char16_t kName[3]{ u'a', u'b', u'c' };
constexpr auto kDescriptor = stringDescriptor(kName);
