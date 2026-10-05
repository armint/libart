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
  - The ESP-IDF component build sets no compile feature and does not define the option. `utils/compat/config.h`
    then chooses from the standard library of the `-std=` flag of the project (see below).

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
  "Requirements"). It does not carry ArduinoJson: a consumer that includes `settings/` or `utils/JSONWriter.h` links
  `ArduinoJson` itself.
- **Dependencies**: the package finds ETL and ArduinoJson with `find_package()`, so both must be installed where
  CMake finds them, for example in the same prefix. The install of libArT installs ArduinoJson as well. ETL installs
  its package only when it is built as the top-level project, so install it separately:
  `cmake -S <etl_source> -B <etl_build> && cmake --install <etl_build> --prefix <prefix>`.

`tests/check-install.sh` does all of this in a temporary prefix, checks that the installed headers are exactly the
headers of `src/` in git without `storage/littlefs/`, and builds the consumer project `tests/install-consumer/`
against it, at C++17 with the own types (`CXX_STANDARD=17`) and at C++23 with the `std`
types (`CXX_STANDARD=23`). The CI job `install` runs both.

## Dependencies

| Dependency | Linked | Notes |
|---|---|---|
| [ETL](https://github.com/ETLCPP/etl) (embedded template library) | **PUBLIC** | Public headers use ETL types, for example `art::span` (see below). With `add_subdirectory()`, a target that links `art::core` gets the ETL include path; the ESP-IDF component exports it as well. The installed package (`find_package(art-core)`) finds the installed ETL package and links `etl::etl` to `art::core` as well. |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | PRIVATE | Used by the JSON reader and writer. |
| [Ryu](https://github.com/ulfjack/ryu) | PRIVATE | Used by `FloatText`, see `cmake/ryu.cmake`. |
| [littlefs](https://github.com/littlefs-project/littlefs) | optional | Only for the `art-storage-littlefs` target (`ART_STORAGE_LITTLEFS`). |

CMake fetches all of them with `FetchContent`.

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
| `utils/compat/span.h` | `art::span`, `art::dynamic_extent` | `std::span` | `etl::span` |
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
  use the same `-std=` flag, so the choice is consistent.

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
| `usb/DescriptorBuilder.h` | `constexpr` builders for the device, configuration, interface association (IAD), interface, endpoint, CDC functional and string descriptors, and `SerialNumber` for a serial number string that is set at run time. |

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
- **Standard requests**: GET_DESCRIPTOR (device, configuration, string), SET_ADDRESS, GET_CONFIGURATION,
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
- **Limits**: one configuration, at most `Device::kMaxInterfaces` (8) interfaces and `Device::kMaxFunctions` (4)
  functions. No heap: all state is in the object (196 bytes on Cortex-M0+).

`tests/usb/FakeDeviceController.h` is a `DeviceController` test double that also plays the host. Use it to test a
`Function` through `Device`.

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
  - a descriptor sequence in `configuration()` or `interfaceDescriptor()` with a `bLength` below 2 or a descriptor that
    runs past the end (`malformedDescriptorSequence`), for example from a hand-written array.

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

### Controller contract

`DeviceController` works on single packets. `startInTransfer()` sends one packet on an IN endpoint, and the
controller reports `inPacketSent()` when the host has acknowledged it. `armOutTransfer()` lets an OUT endpoint
accept one packet, and the controller reports `outPacketReceived()`. An open endpoint that is not armed answers
with NAK. The protocol layer splits transfers into packets and adds zero-length packets (ZLP).

- The controller accepts a SETUP packet also when EP0 is stalled. Before it reports the packet, it sets both
  directions of EP0 to NAK.
- `setEndpointStall(false)` resets the data toggle to DATA0.
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
