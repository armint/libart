// EXPECT: compiles
// Positive control: valid inputs compile with the same flags and include paths as the other snippets.
#include "usb/DescriptorBuilder.h"
#include "usb/MsOs20.h"

using namespace art::usb::descriptor;

constexpr ConfigurationFields makeFields() {
   ConfigurationFields fields{};
   fields.maxPowerMilliamps = 500u;
   return fields;
}

constexpr auto kConfiguration = configuration(makeFields(), interfaceDescriptor(InterfaceFields{}));
constexpr auto kName = stringDescriptor(u"abc");
constexpr auto kMsOs20Set =
art::usb::msos20::descriptorSet(art::usb::msos20::kWindows81,
                                art::usb::msos20::functionSubset(0u,
                                                                 art::usb::msos20::compatibleId("WINUSB"),
                                                                 art::usb::msos20::deviceInterfaceGuid(
                                                                 u"{CDB3B5AD-293B-4663-AA36-1AAE46463776}")));
constexpr auto kBos = bos(art::usb::msos20::platformCapability(0x21u, kMsOs20Set));

void setSerial(art::usb::descriptor::SerialNumber<12> &serial, const std::array<uint8_t, 12> &id) {
   serial.set(id);
}
