# USB device stack

`src/usb/` contains a platform-independent USB 2.0 full-speed device stack, namespace `art::usb`. It has no
hardware dependency, so the protocol code runs in the host tests. A port connects it to the USB peripheral of an MCU.

| Header | Content |
|---|---|
| `usb/UsbTypes.h` | Chapter 9 constants (request codes, descriptor types, feature selectors), `SetupPacket` with `fromBytes()`, `EndpointConfig` and `DeviceState`. |
| `usb/DeviceController.h` | `DeviceController`: the interface to the USB peripheral. `ControllerEvents`: the events that the controller reports to the protocol layer. |
| `usb/Function.h` | `Function`: base class of a USB function (for example CDC-ACM or mass storage) that owns a contiguous range of interfaces. |
| `usb/Device.h` | `Device`: the chapter 9 state machine, the standard requests on EP0 and the routing to the functions. `Descriptors`: the descriptors that `Device` serves. |
| `usb/CdcAcmFunction.h` | `CdcAcmFunction`: the CDC-ACM function (virtual COM port) with its RX and TX queues. |
| `usb/VendorBulkFunction.h` | `VendorBulkFunction`: base of a function with one interface and a bulk OUT and IN endpoint, for a protocol of its own (vendor-specific class). |
| `usb/VendorRequestHandler.h` | `VendorRequestHandler`: the interface of the handler of vendor requests whose recipient is the device. |
| `usb/DescriptorBuilder.h` | `constexpr` builders for the device, configuration, interface association (IAD), interface, endpoint, BOS, CDC functional and string descriptors, and `SerialNumber` for a serial number string that is set at run time. |
| `usb/MsOs20.h` | MS OS 2.0 descriptors (namespace `art::usb::msos20`): `constexpr` builders for the descriptor set and the platform capability of the BOS, and `DescriptorSetHandler`, which answers the request of Windows for the set. |

Use it like this:

```cpp
art::usb::Device device(controller, descriptors); // registers itself as the event handler of the controller
device.addFunction(cdcFunction);                   // before start()
device.start();                                    // pull-up on
```

## `usb::Device`

- **States**: Detached, Powered, Default, Address, Configured and Suspended (USB 2.0 section 9.1.1). The device has
  no VBUS sense, so Attached and Powered are one state. A bus reset returns to Default. Resume returns to the state
  before the suspend.
- **Standard requests**: GET_DESCRIPTOR (device, configuration, string, and BOS if `Descriptors::bos` is set), SET_ADDRESS, GET_CONFIGURATION,
  SET_CONFIGURATION, GET_STATUS, CLEAR_FEATURE and SET_FEATURE (endpoint halt; device remote wakeup only if the
  configuration descriptor declares it), GET_INTERFACE and SET_INTERFACE.
- **STALL**: DEVICE_QUALIFIER and OTHER_SPEED_CONFIGURATION (the device is full-speed only), SET_DESCRIPTOR,
  SYNCH_FRAME, TEST_MODE, unknown interfaces and endpoints, and requests in a state where USB 2.0 does not define
  them. The next SETUP packet ends the stall.
- **SET_ADDRESS**: the new address applies after the status stage, as USB 2.0 section 9.4.6 requires. The
  controller applies it in hardware (see [Controller contract](#controller-contract)).
- **Control transfers**: replies longer than one packet go out in packets of `bMaxPacketSize0`. If a reply is
  shorter than `wLength` and fills its last packet, a ZLP ends the data stage. Host-to-device data stages are
  collected in a buffer of `Device::kControlBufferSize` (64) bytes. A longer data stage stalls, and so does a
  data stage that ends with a short packet before `wLength` bytes.
- **Endpoints**: `Device` reads the configuration descriptor. SET_CONFIGURATION opens the endpoints of alternate
  setting 0 of every interface, and SET_INTERFACE replaces the endpoints of one interface. Both reset the halt and
  the data toggle.
- **Functions**: class and vendor requests to an interface, or to an endpoint of an interface, go to the `Function`
  that owns the interface. So do standard requests to an interface that `Device` does not handle (for example a
  class-specific GET_DESCRIPTOR). The packets of an endpoint go to the function of its interface. Functions send
  with `Device::startInTransfer()`, receive with `Device::armOutTransfer()` and halt with `Device::haltEndpoint()`.
  `Device::cancelInTransfer()` takes back an IN packet that the host has not read.
- **Device vendor requests**: vendor requests whose recipient is the device go to the `VendorRequestHandler` of
  `Device::setVendorRequestHandler()`, in the states Address and Configured. Without a handler they stall. A
  device-to-host reply can point into flash, so it can be longer than the control buffer. Class requests to the
  device stall.
- **Limits**: one configuration, at most `Device::kMaxInterfaces` (8) interfaces and `Device::kMaxFunctions` (4)
  functions. No heap: all state is in the object (208 bytes on Cortex-M0+).

`tests/usb/FakeDeviceController.h` is a `DeviceController` test double that also plays the host. Use it to test a
`Function` through `Device`. With `queueEvents(true)`, it holds the events until `deliverQueuedEvents()`, as a
controller does that queues them in its interrupt handler for the main loop. `tests/usb/TestUsbEnumeration.cpp` runs
the enumeration sequences of Linux and Windows in both modes.

## `usb::CdcAcmFunction`

`CdcAcmFunction<RxSize, TxSize>` is a CDC-ACM (Abstract Control Model) function: a virtual COM port. Linux, macOS
and Windows 10 or later need no driver for it. It owns two interfaces: the communication interface and the data
interface after it. Build its descriptors with `usb/DescriptorBuilder.h`, as in the example of that header.

```cpp
art::usb::CdcAcmFunction<256, 256> serial(0, art::usb::CdcAcmEndpoints{ 0x81, 16, 0x02, 0x82 });
device.addFunction(serial);
// In the main loop:
size_t received = serial.read(buffer);
size_t taken = serial.write(data);
```

- **Class requests**: SET_LINE_CODING and GET_LINE_CODING store and return the line coding (default 115200 8N1).
  The function does not use it. SET_CONTROL_LINE_STATE sets DTR and RTS. DTR set means that a terminal program
  has the port open (`isTerminalOpen()`). SEND_BREAK goes to the event handler. All other requests stall.
- **Event handler**: `setEventHandler()` takes a `CdcAcmEvents` object, for example the UART bridge of a
  USB-to-UART adapter. The function calls it during the control transfer, before the status stage, so keep the
  calls short.
  - `lineCodingRequested()` gets every SET_LINE_CODING, also an unchanged one. It returns `false` to refuse the line
    coding: the request then stalls, and the old line coding stays.
  - `controlLineStateChanged()` gets each change of DTR and RTS, with the new and the old state. A bus reset or
    SET_CONFIGURATION 0 clears a state that is not 0, and the handler sees that change too.
  - `breakRequested()` gets SEND_BREAK with its duration in ms (0xFFFF: until the next SEND_BREAK with 0). It returns
    `false` to stall the request. Declare SEND_BREAK in the descriptor with
    `cdcAcm(cdc::kAcmLineCoding | cdc::kAcmSendBreak)`.
  - Without an event handler, the function accepts every line coding and stalls SEND_BREAK.
- **Queues**: two `etl::circular_buffer` queues, with the capacities `RxSize` and `TxSize` in bytes. `RxSize` must
  be at least 64. `CdcAcmFunctionBase` has the API without the sizes, for code that only reads and writes.
- **TX**: `write()` takes the data that fits into the TX queue and never waits. The function sends the queue in
  packets of 64 bytes. If the last packet is a full packet, a zero-length packet (ZLP) ends the transfer.
- **RX and flow control**: the OUT endpoint accepts a packet only while the RX queue has at least 64 free bytes.
  Otherwise the endpoint NAKs and the host waits until `read()` makes room. No received byte is lost.
- **Endpoint halt**: the function sends and arms nothing on a halted endpoint. After CLEAR_FEATURE(ENDPOINT_HALT)
  on a data endpoint, or SET_INTERFACE on the data interface, it arms the OUT endpoint again and sends the dropped IN
  packet again. The bytes of an IN packet stay in the TX queue until the host acknowledges the packet. So no byte is
  lost or sent twice. A notification on its way is dropped.
- **SERIAL_STATE**: `notifySerialState()` sends the optional notification on the interrupt endpoint.
- **State**: `write()` takes nothing before SET_CONFIGURATION. A bus reset or SET_CONFIGURATION 0 clears both
  queues and the control line state. The line coding stays.
- **Threading**: call `read()` and `write()` from the context that delivers the controller events (normally the
  main loop). The function does no locking.
- **Limits**: the bulk endpoints must have a `wMaxPacketSize` of 64. The `wMaxPacketSize` of the notification
  endpoint must not divide 10 (use 8, 16 or 64), because no ZLP follows the notification.
  `CdcAcmFunction<256, 256>` uses 624 bytes of RAM on Cortex-M0+.

## `usb::VendorBulkFunction`

`VendorBulkFunction` is the base of a function with one interface, one bulk OUT and one bulk IN endpoint. It is for a
protocol of its own, normally in a vendor-specific interface (`classcode::kVendorSpecific`) that the host reaches
with libusb or WinUSB. The base runs the data path; a subclass implements the protocol.

```cpp
class CommandFunction final : public art::usb::VendorBulkFunction {
   public:
   CommandFunction() : VendorBulkFunction(0, art::usb::VendorBulkEndpoints{ 0x03, 0x82, 64 }) {}

   protected:
   bool dataReceived(art::span<const uint8_t> packet) override {
      // Copy the packet. Return false to hold the OUT endpoint until resumeOut().
      return true;
   }
   void transferSent() override {}   // the reply of sendInTransfer() is out
};
```

- **Descriptors**: one interface with two bulk endpoints, for example
  `interfaceDescriptor(InterfaceFields{ 0, 0, classcode::kVendorSpecific, 0, 0, 0 }, endpoint(0x03, Bulk, 64),
  endpoint(0x82, Bulk, 64))`. The endpoints and `maxPacketSize` (8, 16, 32 or 64) of `VendorBulkEndpoints` must match
  them.
- **OUT**: the OUT endpoint is armed after SET_CONFIGURATION. Each packet goes to `dataReceived()`. If it returns
  `true`, the endpoint is armed again at once. If it returns `false`, the endpoint NAKs until the subclass calls
  `resumeOut()`. So a command can wait for the main loop, and no packet is lost. The base does not join packets: the
  protocol knows where a command ends.
- **IN**: `sendInTransfer(data)` sends a buffer of any size in packets of `maxPacketSize` bytes. The buffer is not
  copied: keep it valid until `transferSent()` or `dataPathClosed()`. One transfer at a time: a second call returns
  `false` until `transferSent()`. It also returns `false` if the controller refuses the first packet, for example
  when `maxPacketSize` is larger than the descriptor. On a halted IN endpoint the transfer starts and waits for the
  clear. The hook may start the next transfer.
- **ZLP**: with `ZeroLengthPacket::Auto` (the default), a zero-length packet follows a transfer whose size is a
  multiple of `maxPacketSize`. Use `ZeroLengthPacket::Never` when the host reads exactly the size of the transfer:
  then the ZLP would end the next read of the host. An empty buffer sends one ZLP in both modes.
- **Endpoint halt**: the subclass halts an endpoint with `device()->haltEndpoint()`, for example on a protocol
  error. After CLEAR_FEATURE(ENDPOINT_HALT) or SET_INTERFACE, the function arms the OUT endpoint again (unless it is
  held) and sends the dropped IN packet again. The transfer continues where it stopped.
- **State**: `dataPathOpened()` follows SET_CONFIGURATION. A bus reset or SET_CONFIGURATION 0 drops the IN transfer,
  ends the hold of the OUT endpoint and calls `dataPathClosed()`; no `transferSent()` follows for the dropped
  transfer.
- **Vendor requests**: override `controlRequest()` for vendor requests on EP0. The data path hooks of `Function` are
  `final`.
- **Threading**: call `sendInTransfer()` and `resumeOut()` from the context that delivers the controller events
  (normally the main loop). The hooks run there too. The function does no locking.
- **Limits**: no buffer of its own; the base uses 40 bytes of RAM on Cortex-M0+.

## Descriptor builders

The builders in `usb/DescriptorBuilder.h` (namespace `art::usb::descriptor`) return `std::array<uint8_t, N>` in
wire format. Use them in `constexpr` variables, so that the descriptors are built by the compiler and stay in flash.

- `configuration()` and `interfaceDescriptor()` take the descriptors that follow them as arguments. They compute
  `wTotalLength`, `bNumInterfaces` (the interfaces with alternate setting 0) and `bNumEndpoints`.
- `configuration()` sets bit 7 of `bmAttributes` and converts `maxPowerMilliamps` to units of 2 mA.
- `stringDescriptor(u"...")` takes a UTF-16 literal of at most 126 code units. `languages(kLanguageEnglishUs)` builds
  string descriptor 0.
- `SerialNumber<IdBytes>` holds a string descriptor in RAM. `set(id)` writes each byte of `id` as two upper-case
  hexadecimal digits, for example the 12-byte UID of an STM32 as a 24-digit serial number. `id` is a
  `std::array`, a C array or a fixed-extent span of exactly `IdBytes` bytes, so an ID of another length does not
  compile. A span of dynamic extent does not compile either. Call `set()` before `Device::start()`: the host
  can read the string at any time after the device connects.
- `asSpan()` makes the spans for `Descriptors`.
- Build the descriptors in `constexpr` variables. These errors then fail the build, with an error that names a
  function in `detail::`:
  - `maxPowerMilliamps` above 500 mA (`maxPowerAbove500Milliamps`);
  - a `char16_t` array for `stringDescriptor()` whose last element is not a null (`stringDescriptorNeedsNullTerminatedLiteral`);
  - a descriptor sequence in `configuration()`, `interfaceDescriptor()` or `bos()` with a `bLength` below 2 or a
    descriptor that runs past the end (`malformedDescriptorSequence`), for example from a hand-written array.

  These functions are not `constexpr`. At run time they trap (`__builtin_trap()`), so a builder that runs at run time
  with such a value traps instead of returning a wrong descriptor. Valid values work at run time at every
  optimization level.
- `tests/usb/compile_fail/check.cmake` checks that each of these errors, and an ID of the wrong length for
  `SerialNumber::set()`, fails the build with the expected diagnostic. A positive control must compile. Run it with
  `cmake -D CXX=g++ -D ETL_INCLUDE_DIR=<build>/_deps/etl-src/include -P tests/usb/compile_fail/check.cmake`. The C++17
  cells of the CI job `test` run it.
- The builders do not check other field values. The application supplies its own values, for example `idVendor`,
  `idProduct`, `bcdDevice` and the maximum power.

```cpp
using namespace art::usb::descriptor;
constexpr auto kConfiguration = configuration(ConfigurationFields{ 1, 0, true, false, 100 },
   interfaceAssociation(0, 2, classcode::kCdc, cdc::kSubclassAcm, 0),
   interfaceDescriptor(InterfaceFields{ 0, 0, classcode::kCdc, cdc::kSubclassAcm, 0, 0 },
      cdcHeader(), cdcCallManagement(0, 1), cdcAcm(), cdcUnion(0, 1),
      endpoint(0x81, art::usb::EndpointType::Interrupt, 16, 16)),
   interfaceDescriptor(InterfaceFields{ 1, 0, classcode::kCdcData, 0, 0, 0 },
      endpoint(0x02, art::usb::EndpointType::Bulk, 64), endpoint(0x82, art::usb::EndpointType::Bulk, 64)));
```

## MS OS 2.0 descriptors

With MS OS 2.0 descriptors, Windows 8.1 and later bind a driver to a device, or to one function of a composite
device, with no INF file and no user action. The normal use is WinUSB for a vendor-specific interface, for example
a `VendorBulkFunction`. `usb/MsOs20.h` (namespace `art::usb::msos20`) has the parts:

1. Set `bcdUSB` to 0x0201 in `DeviceFields`. Windows then asks for the BOS descriptor.
2. Build the descriptor set: `descriptorSet()` with one `functionSubset()` for each function that gets WinUSB.
   The subset has the first interface of the function, `compatibleId("WINUSB")` and `deviceInterfaceGuid(u"{...}")`.
   libusb and other WinUSB clients need the GUID to find the interface. A function without a subset, for example
   CDC-ACM, keeps the driver that Windows selects from its class. A device that is not composite puts the feature
   descriptors directly into `descriptorSet()`.
3. Build the BOS: `descriptor::bos(platformCapability(vendorCode, set))`. The platform capability takes the size and
   the Windows version from the set. Put the BOS into `Descriptors::bos`.
4. Register a `DescriptorSetHandler(vendorCode, asSpan(set))` with `Device::setVendorRequestHandler()`. It answers
   the vendor request of Windows (bRequest `vendorCode`, wIndex 7) and stalls all other requests.

```cpp
using namespace art::usb;
constexpr uint8_t kVendorCode = 0x21;
constexpr auto kMsOs20Set = msos20::descriptorSet(msos20::kWindows81,
   msos20::functionSubset(0, msos20::compatibleId("WINUSB"),
      msos20::deviceInterfaceGuid(u"{CDB3B5AD-293B-4663-AA36-1AAE46463776}")));
constexpr auto kBos = descriptor::bos(msos20::platformCapability(kVendorCode, kMsOs20Set));
msos20::DescriptorSetHandler msOs20Handler(kVendorCode, descriptor::asSpan(kMsOs20Set));
```

A device with one configuration puts the function subsets directly after the set header, as above.
`configurationSubset()` is only for a device with more than one configuration, which `usb::Device` does not support.
The builders check their input like the other builders: a compatible ID of more than 8 characters, a GUID without
braces, or a malformed set fails the build. Windows keeps data about the descriptors of a device in the registry key
`HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<vvvvpppprrrr>` (VID, PID and `bcdDevice` in hexadecimal). A
driver that was installed for the device before (for example with Zadig) has priority over the WinUSB compatible ID.
After a change of the descriptors, uninstall the device and its driver in the Device Manager and delete that key.

## Controller contract

`DeviceController` works on single packets. `startInTransfer()` sends one packet on an IN endpoint, and the
controller reports `inPacketSent()` when the host has acknowledged it. `armOutTransfer()` lets an OUT endpoint
accept one packet, and the controller reports `outPacketReceived()`. An open endpoint that is not armed answers
with NAK. The protocol layer splits transfers into packets and adds zero-length packets (ZLP).

- The controller accepts a SETUP packet also when EP0 is stalled. Before it reports the packet, it sets both
  directions of EP0 to NAK.
- `setEndpointStall(false)` resets the data toggle to DATA0.
- `setEndpointStall(true)` and `setEndpointStall(false)` drop an IN packet that the host has not acknowledged,
  and the arm of an OUT endpoint. No event is reported for them. After `setEndpointStall(false)` the endpoint NAKs
  until the next `startInTransfer()` or `armOutTransfer()`. A function restarts its transfers in
  `Function::endpointHaltCleared()`.
- `cancelInTransfer()` drops an IN packet that the host has not acknowledged. No event is reported for it, and the
  data toggle does not change. The endpoint NAKs until the next `startInTransfer()`. A packet that the host
  acknowledged before the call keeps its event. Then `cancelInTransfer()` returns false, and the caller waits for
  `inPacketSent()` before it sends the next packet. Limit: if the host received the packet but its ACK was lost, the
  host discards the next packet as a repeat. `Device::cancelInTransfer()` does not touch a halted endpoint.
- `Device` calls `setAddress()` when it accepts SET_ADDRESS, before the status stage. The controller applies the
  address in hardware when the host acknowledges the zero-length status packet on EP0 (STM32 FS: in the handler of
  that IN completion). A peripheral that needs the address before the status stage writes it at once. The
  controller drops a pending address on the next SETUP packet and on a bus reset. With queued events, the main loop
  sees the acknowledgement too late for the host, which uses the new address 2 ms after the status stage.
- A control endpoint is opened once, with its OUT address, and covers both directions.

## Threading

The events and the calls run in one context, normally the main loop. They must not overlap. A controller that
takes the events in an interrupt handler queues them, and delivers them from its `poll()` in the main loop. While
the main loop has not armed an endpoint, the endpoint answers with NAK. Slow processing therefore only reduces the
throughput; it does not break the protocol.
