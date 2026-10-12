#include "FakeDeviceController.h"
#include "usb/DescriptorBuilder.h"
#include "usb/Device.h"
#include "usb/Function.h"
#include "usb/MsOs20.h"
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using namespace art::usb;
using namespace art::usb::descriptor;

namespace {

template <size_t Size>
std::vector<uint8_t> bytesOf(const std::array<uint8_t, Size> &descriptor) {
   return std::vector<uint8_t>(descriptor.begin(), descriptor.end());
}

constexpr uint8_t kVendorCode = 0x21u;

// The function subset of the bulk interface of DAPLink (source/usb/usb_lib.c, USBD_WinUSBDescriptorSetDescriptor,
// USBD_BULK_ENABLE), a CMSIS-DAP v2 probe that Windows binds to WinUSB: interface 0, compatible ID "WINUSB" and the
// device interface GUID of CMSIS-DAP v2.
// clang-format off
const std::vector<uint8_t> kReferenceFunctionSubset{
   0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0xA0, 0x00,
   0x14, 0x00, 0x03, 0x00, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0x84, 0x00, 0x04, 0x00, 0x07, 0x00, 0x2A, 0x00,
   'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0,
   'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
   0x50, 0x00,
   '{', 0, 'C', 0, 'D', 0, 'B', 0, '3', 0, 'B', 0, '5', 0, 'A', 0, 'D', 0, '-', 0, '2', 0, '9', 0, '3', 0, 'B', 0,
   '-', 0, '4', 0, '6', 0, '6', 0, '3', 0, '-', 0, 'A', 0, 'A', 0, '3', 0, '6', 0, '-', 0, '1', 0, 'A', 0, 'A', 0,
   'E', 0, '4', 0, '6', 0, '4', 0, '6', 0, '3', 0, '7', 0, '7', 0, '6', 0, '}', 0, 0, 0, 0, 0,
};
// clang-format on

constexpr auto kFunctionSubset =
msos20::functionSubset(0u, msos20::compatibleId("WINUSB"), msos20::deviceInterfaceGuid(u"{CDB3B5AD-293B-4663-AA36-1AAE46463776}"));
constexpr auto kDescriptorSet = msos20::descriptorSet(msos20::kWindows81, kFunctionSubset);
constexpr auto kBos = bos(msos20::platformCapability(kVendorCode, kDescriptorSet));

constexpr uint8_t kVendorDeviceIn = 0xC0u;
constexpr uint8_t kGetDescriptor = 6u;
constexpr uint8_t kSetAddress = 5u;

} // namespace

TEST_CASE("[usb::msos20] the function subset matches the reference") {
   CHECK(kFunctionSubset.size() == 160u);
   CHECK(bytesOf(kFunctionSubset) == kReferenceFunctionSubset);
}

TEST_CASE("[usb::msos20] descriptor set header") {
   static_assert(kDescriptorSet.size() == 170u, "set header and one function subset");
   const std::vector<uint8_t> header(kDescriptorSet.begin(), kDescriptorSet.begin() + 10);
   CHECK(header == std::vector<uint8_t>{ 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0xAA, 0x00 });
   CHECK(std::vector<uint8_t>(kDescriptorSet.begin() + 10, kDescriptorSet.end()) == kReferenceFunctionSubset);
}

TEST_CASE("[usb::msos20] configuration subset and compatible ID fields") {
   constexpr auto subset = msos20::configurationSubset(1u, msos20::compatibleId("WINUSB", "SUB"));
   CHECK(bytesOf(subset) == std::vector<uint8_t>{ 0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x1C, 0x00, 0x14, 0x00,
                                                  0x03, 0x00, 'W',  'I',  'N',  'U',  'S',  'B',  0,    0,
                                                  'S',  'U',  'B',  0,    0,    0,    0,    0 });
   // An ID of exactly 8 characters has no null.
   CHECK(bytesOf(msos20::compatibleId("ABCDEFGH"))[4u + 7u] == 'H');
}

TEST_CASE("[usb::msos20] BOS with the platform capability") {
   // clang-format off
   const std::vector<uint8_t> expected{
      0x05, 0x0F, 0x21, 0x00, 0x01,
      0x1C, 0x10, 0x05, 0x00,
      0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
      0x00, 0x00, 0x03, 0x06, 0xAA, 0x00, kVendorCode, 0x00,
   };
   // clang-format on
   CHECK(bytesOf(kBos) == expected);
}

TEST_CASE("[usb::msos20] BOS counts every device capability") {
   constexpr std::array<uint8_t, 3> other{ 0x03, 0x10, 0x02 };
   constexpr auto twoCapabilities = bos(msos20::platformCapability(kVendorCode, kDescriptorSet), other);
   CHECK(twoCapabilities[2] == 36u);
   CHECK(twoCapabilities[4] == 2u);
}

TEST_CASE("[usb::msos20] DescriptorSetHandler answers the request of Windows") {
   // A device with only a vendor-specific interface 0, bcdUSB 2.01.
   DeviceFields deviceFields{};
   deviceFields.bcdUSB = 0x0201u;
   const auto deviceDescriptor = device(deviceFields);
   constexpr auto configurationDescriptor =
   configuration(ConfigurationFields{},
                 interfaceDescriptor(InterfaceFields{ 0u, 0u, classcode::kVendorSpecific, 0u, 0u, 0u },
                                     endpoint(0x01u, EndpointType::Bulk, 64u), endpoint(0x81u, EndpointType::Bulk, 64u)));
   Descriptors descriptors{};
   descriptors.device = asSpan(deviceDescriptor);
   descriptors.configuration = asSpan(configurationDescriptor);
   descriptors.bos = asSpan(kBos);

   test::FakeDeviceController controller;
   Device usbDevice(controller, descriptors);
   msos20::DescriptorSetHandler handler(kVendorCode, asSpan(kDescriptorSet));
   usbDevice.setVendorRequestHandler(&handler);
   usbDevice.start();
   controller.busReset();

   bool ok = false;
   CHECK_FALSE(controller.controlOut(test::setupPacket(0x00u, kSetAddress, 3u, 0u, 0u), art::span<const uint8_t>(), &ok).stalled);

   // The sequence of Windows: the BOS header, the whole BOS, then the descriptor set with the vendor code.
   const auto bosHeader = controller.controlIn(test::setupPacket(0x80u, kGetDescriptor, 0x0F00u, 0u, 5u), &ok);
   CHECK(ok);
   REQUIRE(bosHeader.data.size() == 5u);
   const uint16_t bosLength = static_cast<uint16_t>(bosHeader.data[2] | (bosHeader.data[3] << 8u));
   CHECK(controller.controlIn(test::setupPacket(0x80u, kGetDescriptor, 0x0F00u, 0u, bosLength), &ok).data == bytesOf(kBos));
   const auto set =
   controller.controlIn(test::setupPacket(kVendorDeviceIn, kVendorCode, 0u, msos20::kDescriptorIndex, 170u), &ok);
   CHECK(ok);
   CHECK(set.data == bytesOf(kDescriptorSet));

   SUBCASE("other requests stall") {
      CHECK(controller
            .controlIn(test::setupPacket(kVendorDeviceIn, static_cast<uint8_t>(kVendorCode + 1u), 0u, msos20::kDescriptorIndex, 170u))
            .stalled);
      CHECK(
      controller.controlIn(test::setupPacket(kVendorDeviceIn, kVendorCode, 0u, msos20::kSetAltEnumerationIndex, 170u)).stalled);
      CHECK(controller.controlIn(test::setupPacket(kVendorDeviceIn, kVendorCode, 1u, msos20::kDescriptorIndex, 170u)).stalled);
      CHECK(controller
            .controlOut(test::setupPacket(0x40u, kVendorCode, 0u, msos20::kDescriptorIndex, 0u), art::span<const uint8_t>())
            .stalled);
   }
}
