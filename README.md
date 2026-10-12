# libArT

Core utility library for embedded systems (Cortex-M and RISC-V microcontrollers, bare-metal or with an RTOS):
streams, logging, Modbus, settings, storage and utilities. `AGENTS.md` describes the components in detail.

## Requirements

- C++17 or newer, with GCC 11 or later or Clang 9 or later (`utils/compat/bit.h` needs `__builtin_bit_cast`). CI
  builds and tests with GCC at C++17, C++20 and C++23, and with Clang at C++17 and C++23. These minimum versions are
  not tested: CI uses GCC 14.2, Clang 20.1 and arm-none-eabi-gcc 14.3 (`firmware-builder:v1.14.0`).
- With `ART_USE_STD_EXPECTED_SPAN=ON` (the default unless `CMAKE_CXX_STANDARD` is below 23, see below), C++23:
  `art-core`, `art-rtos` and `art-storage-littlefs` then require `cxx_std_23`. With `OFF` they require
  `cxx_std_17`. Both are minimums, not pins: a superproject may build at a newer standard.
  - A build without `CMAKE_CXX_STANDARD` keeps `ON`, so art-core and its consumers build at C++23.
  - A build without `CMAKE_CXX_STANDARD` that sets `OFF` builds at the default standard of the compiler (for
    example gnu++17 for GCC 14), not at C++23.
  - A consumer that sets `-DART_USE_STD_EXPECTED_SPAN=OFF` at C++23 gets the minimum `cxx_std_17`. It still builds
    at C++23, but with the own types.
  - `ON` with a `CMAKE_CXX_STANDARD` below 23 stops the configure step with an error.
  - The ESP-IDF component build sets no compile feature and does not define the option. `utils/compat/config.h`
    then chooses from the standard library of the `-std=` flag of the project (see below).
- With `OFF`, `art::span` is the span of [span-compat](https://gitlab.vandertogt.org/embedded-libraries/span-compat).
  CMake fetches span-compat only in this case. A build with `ON` does not fetch or link it. The ESP-IDF component
  is an exception: it always fetches span-compat. See
  "span-compat: one span type per build" below.

## Using libArT in a CMake project

Add the repository with `add_subdirectory()` and link `art::core` (and `art::rtos` for the FreeRTOS wrappers).
ESP-IDF projects use the repository as a component.

### Installed package

`cmake --install <build_dir> --prefix <prefix>` installs `libart-core.a`, the CMake package `art-core` and the
public headers. After `find_package(art-core)`, link `art::core` and `art::rtos`, the same names as with
`add_subdirectory()`.

- **Headers**: they are installed to `<prefix>/include/libart/`, so that generic names such as `utils/` or
  `art_types.h` do not collide with other packages in a shared prefix. The include directory of `art::core` and
  `art::rtos` is `<prefix>/include/libart`, and the headers keep their path below `src/`, so a consumer includes them
  as with `add_subdirectory()`, for example `#include "streams/Readable.h"` (installed to
  `<prefix>/include/libart/streams/Readable.h`). Only the headers
  (`*.h`, `*.hpp`) of `src/` are installed, and not those of `storage/littlefs/`: the `art-storage-littlefs` target
  is not part of the installed package, so use `add_subdirectory()` for it. The headers in `freertos/` are
  installed for `art::rtos`; the consumer provides FreeRTOS.
- **Usage requirements**: as with `add_subdirectory()`, `art::core` carries the ETL include path, the compile
  definition `ART_USE_STD_EXPECTED_SPAN` and the minimum C++ standard of the build that was installed (see
  "Requirements"). With `OFF`, it also carries `span-compat::span-compat`, with the definition
  `SPAN_COMPAT_USE_STD_SPAN=0`. It does not carry ArduinoJson: a consumer that includes `settings/` or
  `utils/JSONWriter.h` links `ArduinoJson` itself.
- **Dependencies**: the package finds ETL and ArduinoJson with `find_package()`, so both must be installed where
  CMake finds them, for example in the same prefix. The install of libArT installs ArduinoJson as well. ETL installs
  its package only when it is built as the top-level project, so install it separately:
  `cmake -S <etl_source> -B <etl_build> && cmake --install <etl_build> --prefix <prefix>`.
- **span-compat**: with `OFF`, the install of libArT also installs the span-compat package, configured with
  `SPAN_COMPAT_USE_STD_SPAN=OFF`. `find_package(art-core)` then finds it. It stops with an error if the span-compat
  package that it finds does not use its own span. With `ON`, the package does not need span-compat.

`tests/check-install.sh` does all of this in a temporary prefix, checks that the installed headers are exactly the
headers of `src/` in git without `storage/littlefs/`, and builds the consumer project `tests/install-consumer/`
against it, at C++17 with the own types (`CXX_STANDARD=17`) and at C++23 with the `std`
types (`CXX_STANDARD=23`). It also checks that span-compat is in the prefix only at C++17. The CI job `install`
runs both.

## Dependencies

| Dependency | Linked | Notes |
|---|---|---|
| [ETL](https://github.com/ETLCPP/etl) (embedded template library) | **PUBLIC** | Public headers use ETL types, for example `storage/FileSystem.h`. With `add_subdirectory()`, a target that links `art::core` gets the ETL include path; the ESP-IDF component exports it as well. The installed package (`find_package(art-core)`) finds the installed ETL package and links `etl::etl` to `art::core` as well. |
| [span-compat](https://gitlab.vandertogt.org/embedded-libraries/span-compat) | **PUBLIC**, only with `ART_USE_STD_EXPECTED_SPAN=OFF` | `art::span` is its span. The pin is a commit in `cmake/span-compat.cmake`. See "span-compat: one span type per build". |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | PRIVATE | Used by the JSON reader and writer. |
| [Ryu](https://github.com/ulfjack/ryu) | PRIVATE | Used by `FloatText`, see `cmake/ryu.cmake`. |
| [littlefs](https://github.com/littlefs-project/littlefs) | optional | Only for the `art-storage-littlefs` target (`ART_STORAGE_LITTLEFS`). |

CMake fetches all of them with `FetchContent`. span-compat is a private repository of the GitLab server. To build
without access to it, set `FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT` to a local checkout of span-compat. The ESP-IDF
component fetches span-compat also with the `std` types (see "`ART_USE_STD_EXPECTED_SPAN`" below).

**`ETL_NO_STL` is not supported.** With `ETL_NO_STL`, ETL defines its own `std::initializer_list`, which conflicts
with the standard library headers that libArT includes.

## Formatting

The sources in `src/` and `tests/` are formatted with clang-format 20 and the `.clang-format` of the repository.
Third-party sources are listed in `.clang-format-ignore` and keep the format of their upstream.

- `cmake --build <build_dir> --target clangformat` formats all files in place.
- `cmake --build <build_dir> --target clangformat-check` changes nothing and fails if a file is not formatted. The CI
  job `format` runs it.
- `git clang-format-20 origin/dev` formats only the lines that you changed.

The targets exist only when CMake finds clang-format 18 or later and libArT is the top-level project (see
`cmake/clang-format.cmake`). Other major versions than 20 can format differently.

The whole tree was reformatted in one commit. To make `git blame` skip that commit, run once per clone:

```sh
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

## CMake options

| Option | Default | Effect |
|---|---|---|
| `ART_USE_STD_EXPECTED_SPAN` | `ON`, unless `CMAKE_CXX_STANDARD` is below 23 | Selects what `art::expected`, `art::unexpected` and `art::span` are, and the minimum standard of the targets (`ON`: C++23, `OFF`: C++17). See below. |
| `ART_STORAGE_LITTLEFS` | `OFF` | Builds the LittleFS file system binding (`art-storage-littlefs`). |
| `ART_LITTLEFS_NO_ASSERT` | `ON` when cross-compiling | Builds littlefs with `LFS_NO_ASSERT`. |
| `ART_CLANG_FORMAT` | `clang-format-20`, else `clang-format` | The clang-format executable of the `clangformat` and `clangformat-check` targets (see `cmake/clang-format.cmake`). |

### `ART_USE_STD_EXPECTED_SPAN`: `std` or own vocabulary types

The headers in `src/utils/compat/` let the same code compile at C++17 and at C++23:

| Header | Name | `ART_USE_STD_EXPECTED_SPAN=ON` | `ART_USE_STD_EXPECTED_SPAN=OFF` |
|---|---|---|---|
| `utils/compat/expected.h` | `art::expected`, `art::unexpected`, `art::unexpect` | `std::expected`, ... | libArT's own minimal implementation |
| `utils/compat/span.h` | `art::span`, `art::dynamic_extent` | `std::span` | `span_compat::span`, the own span of span-compat |
| `utils/compat/bit.h` | `art::bit_cast`, `art::is_little_endian` | a `constexpr` `bit_cast` on `__builtin_bit_cast` (GCC 11, Clang 9 and later), the same entity at every standard | same |
| `utils/compat/type_traits.h` | `art::remove_cvref`, `art::remove_cvref_t` | an own struct at every standard (the same type as `std::remove_cvref_t`) | same |

The option is exported as the **PUBLIC** compile definition `ART_USE_STD_EXPECTED_SPAN=1` or `=0` of `art-core`
(and `art-rtos`). libArT, libArT-proto and the application therefore all see the same choice. The choice must be
the same for the whole program: `Readable` and `Writable` have virtual functions that return `expected` and take
`span`, and a program that mixes both choices violates the one-definition rule without any diagnostic.

- `ON` needs `std::expected` and `std::span` in **every** translation unit that includes a compat header, that is
  C++23. The compat headers stop the build with `#error` otherwise.
- `OFF` works at C++17 and later.
- The ESP-IDF component build has no `CMAKE_CXX_STANDARD` and does not define `ART_USE_STD_EXPECTED_SPAN`.
  `utils/compat/config.h` then derives it from the language standard of the compiler: `std` types if the standard
  library provides `std::expected` and `std::span`, own types otherwise. All translation units of an ESP-IDF project
  use the same `-std=` flag, so the choice is consistent. The component defines `SPAN_COMPAT_USE_STD_SPAN=0`
  **PUBLIC**, so the span of span-compat is the own span for all components that require libArT. Thus C++17, C++20
  and C++23 work.
- The ESP-IDF component always fetches span-compat, also at C++23. CMake does not know the standard of the
  translation units, so it cannot know if `art::span` needs span-compat. Thus an ESP-IDF project needs access to the
  span-compat repository, or `FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT`.

### span-compat: one span type per build

`system-stm32` must not depend on libArT, so it uses span-compat for the spans of its public interface. At C++17,
`art::span` must be the same type, or one program contains two span types. span-compat has the CMake option
`SPAN_COMPAT_USE_STD_SPAN` (`AUTO`, `ON` or `OFF`). It sets the span type for every target that links span-compat.
libArT sets this option. libArT does not define the macro itself.

| `ART_USE_STD_EXPECTED_SPAN` | `art::span` | span-compat | `SPAN_COMPAT_USE_STD_SPAN` |
|---|---|---|---|
| `ON` (C++23) | `std::span` | not fetched, not linked | must be `ON` in a superproject that adds span-compat; `AUTO` or `OFF` stops the configure |
| `OFF` (C++17 or later) | `span_compat::span`, the own span | fetched and linked **PUBLIC** | must be `OFF`; libArT sets `OFF` when it is not set |

- With `OFF`, libArT uses the own span also at C++20 or later. Thus `art::span` does not change when one target of
  the build uses a newer standard.
- A superproject that adds span-compat itself (for example for system-stm32) must add it before libArT, with
  `FetchContent`, `add_subdirectory()` or `find_package(span-compat)`. Set `-DSPAN_COMPAT_USE_STD_SPAN=OFF` when
  `ART_USE_STD_EXPECTED_SPAN` is `OFF`, or use a span-compat package that was installed with `OFF`. libArT then uses
  that span-compat and does not fetch a second one. Any other value stops the configure step with an error.
- libArT reads the span type from the definition `SPAN_COMPAT_USE_STD_SPAN` on the target
  `span-compat::span-compat`. Thus the check is the same for a span-compat from `add_subdirectory()` and for an
  installed package.
- With `ON`, a span-compat in the same build must be `ON`. `AUTO` is not sufficient: it gives the own span to a
  target below C++20, for example system-stm32 at C++17. `ON` gives `std::span` to every target and raises the
  targets that link span-compat to C++20. libArT checks this at the end of the configure step.
- `utils/compat/span.h` has an `#error` for the case that CMake cannot see: `ART_USE_STD_EXPECTED_SPAN=0` while
  span-compat selects `std::span`.

`tests/check-subproject.sh` adds libArT to the superproject `tests/subproject-consumer/` with `add_subdirectory()`.
An application passes `art::span` to a library that links only span-compat: at C++17 with span-compat `OFF` (from
`FetchContent` and from an installed package), and at C++23 with span-compat `ON`. At C++23 without that library,
the script checks that span-compat is not fetched. It also checks that five mixed configurations stop the configure
step. The CI job `subproject` runs it.

The own `art::expected` is used instead of `etl::expected` because `etl::expected` stores its value in an
`etl::variant`: `sizeof(etl::expected<char, E>)` is 24 bytes (2 bytes for `std::expected`), and the code of a
typical call chain grows by 69 %. The own type has the same size and code size as `std::expected`. It implements
the subset that libArT uses: construction from a value, from `unexpected` and with `unexpect`, `expected<void, E>`,
`has_value()`, `operator bool`, `*`, `->`, `value()`, `error()`, `value_or()` and comparison. It is trivially
copyable when `T` and `E` are, and it supports move-only `T` (for example `storage::File`). `*`, `->` and
`error()` do not check the state, like `std::expected`.

The comparisons of the own `art::expected` give the results of `std::expected`. They also do not warn where
`std::expected` is silent (`std::expected` does not warn because libstdc++ is a system header), with one known
limit: an unscoped enum compared with an `int` or with another enum type can still give `-Wsign-compare` or
`-Wenum-compare` inside `utils/compat/expected.h`. The result is still the one of `std::expected`. To avoid the
warning, compare with a value of the same enum type, or cast explicitly.

`value()` on an error fails differently in the two modes. Do not rely on either behaviour; check `has_value()`
first:

- Own `art::expected` (`OFF`): `value()` calls `__builtin_trap()`, which raises an illegal-instruction or
  breakpoint fault.
- `std::expected` (`ON`): `value()` throws `std::bad_expected_access`. In a build with `-fno-exceptions`,
  libstdc++ calls `abort()` instead.

## USB device stack

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

### `usb::Device`

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
  controller applies it in hardware (see "Controller contract").
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

### `usb::CdcAcmFunction`

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

### `usb::VendorBulkFunction`

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

### Descriptor builders

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

### MS OS 2.0 descriptors

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
braces, or a malformed set fails the build. Windows stores the result per VID, PID and `bcdDevice`. After a change of
the descriptors, change `bcdDevice` or remove the device in the Device Manager, so that Windows reads them again.

### Controller contract

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
- `Device` calls `setAddress()` when it accepts SET_ADDRESS, before the status stage. The controller applies the
  address in hardware when the host acknowledges the zero-length status packet on EP0 (STM32 FS: in the handler of
  that IN completion). A peripheral that needs the address before the status stage writes it at once. The
  controller drops a pending address on the next SETUP packet and on a bus reset. With queued events, the main loop
  sees the acknowledgement too late for the host, which uses the new address 2 ms after the status stage.
- A control endpoint is opened once, with its OUT address, and covers both directions.

### Threading

The events and the calls run in one context, normally the main loop. They must not overlap. A controller that
takes the events in an interrupt handler queues them, and delivers them from its `poll()` in the main loop. While
the main loop has not armed an endpoint, the endpoint answers with NAK. Slow processing therefore only reduces the
throughput; it does not break the protocol.
