// EXPECT: compiles
// Positive control: valid inputs compile with the same flags and include paths as the other snippets.
#include "usb/DescriptorBuilder.h"

using namespace art::usb::descriptor;

constexpr ConfigurationFields makeFields() {
   ConfigurationFields fields{};
   fields.maxPowerMilliamps = 500u;
   return fields;
}

constexpr auto kConfiguration = configuration(makeFields(), interfaceDescriptor(InterfaceFields{}));
constexpr auto kName = stringDescriptor(u"abc");

void setSerial(art::usb::descriptor::SerialNumber<12> &serial, const std::array<uint8_t, 12> &id) {
   serial.set(id);
}
