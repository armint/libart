// EXPECT: maxPowerAbove500Milliamps
#include "usb/DescriptorBuilder.h"

using namespace art::usb::descriptor;

constexpr ConfigurationFields makeFields() {
   ConfigurationFields fields{};
   fields.maxPowerMilliamps = 501u;
   return fields;
}

constexpr auto kConfiguration = configuration(makeFields());
