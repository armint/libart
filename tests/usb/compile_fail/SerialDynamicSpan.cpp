// EXPECT: no matching (member )?function
#include "usb/DescriptorBuilder.h"

void setSerial(art::usb::descriptor::SerialNumber<12> &serial, art::span<const uint8_t> id) {
   serial.set(id);
}
