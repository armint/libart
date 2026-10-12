// EXPECT: deviceInterfaceGuidNeedsBracedLiteral
#include "usb/MsOs20.h"

constexpr auto kGuid = art::usb::msos20::deviceInterfaceGuid(u"(CDB3B5AD-293B-4663-AA36-1AAE46463776)");
