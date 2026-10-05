// EXPECT: no matching (member )?function
#include "usb/DescriptorBuilder.h"

void setSerial(art::usb::descriptor::SerialNumber<12> &serial, const std::array<uint8_t, 8> &id) {
   serial.set(id);
}
