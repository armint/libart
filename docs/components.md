# Components

This document gives an overview of the components of libArT. The header of each class has the full API
documentation. Include the headers with their path below `src/`, for example `#include "streams/Readable.h"`.

| Directory | Namespace | Content |
|---|---|---|
| `streams/` | global | `Readable` and `Writable` stream interfaces and their implementations. |
| `logging/` | global (`LogManager`), `logging` | `LogManager`, `Logger` and log formatters. |
| `modbus/` | `modbus` | `ModbusMaster` and `ModbusSlave` for Modbus RTU. |
| `settings/` | global | `SettingsManager` and `Setting<T>`: device settings stored as JSON. |
| `storage/` | `art::storage`; `FatFileSystem`, `FatFile` and `FatDir` are global | Block devices and file systems (FatFs, LittleFS). See [Storage](storage.md). |
| `usb/` | `art::usb` | USB full-speed device stack. See [USB device stack](usb-device-stack.md). |
| `sensors/` | global | Temperature sensors: thermocouples (K and S type), cold-junction compensation and thermistors. |
| `freertos/` | `art::rtos` | C++ wrappers for FreeRTOS (target `art::rtos`). |
| `utils/` | global; `utils::FloatText`; `art` for `utils/compat/` | CRC and SHA-256 digests, JSON, `FloatText`, `tfp_printf`, ring buffers, compat headers. |

No class of libArT uses the heap. Buffers are members of the object, or the caller supplies them as a span.

## Streams

`Readable` (`streams/Readable.h`) is a source of bytes. `Writable` (`streams/Writable.h`) is a sink of bytes. Both
return `art::expected` values: the result on success, or a `Readable::Error` or `Writable::Error`.

- `Writable` has `printf()` and `println()`. They format directly into `write()` with `tfp_printf`, without a
  buffer. `%f` is available only when `PRINTF_ENABLE_FLOATING_POINT` is defined.
- `Readable` reads numbers in a selected byte order, and decodes variable-length integers (LEB128).
- Memory: `ArrayReadable`, `ArrayWritable`, `StringReadable` and `StringWritable`.
- Wrappers: `BufferedReadable`, `BufferedWritable`, `BoundedReadable`, `CountingWritable`, `HexReadable`,
  `CharacterSkippingReadable`, `DigestReadable` and `DigestWritable` (calculate a CRC or SHA-256 of the data that
  passes).
- Files: `FileReadable` and `FileWritable` adapt an `art::storage::File` of any `FileSystem`. `FileWritable` has no
  buffer, so wrap it in a `BufferedWritable`. `FatFileReadable` and `FatFileWritable` are for `FatFile`.
- Other: `NullWritable`, `PseudoRandomReadable`, `AsymmetricKeyReadable` and `AsymmetricKeyWritable`.

## Logging

A `Logger` has a topic and a level. It registers itself with the `LogManager` singleton when you construct it. The
`LogManager` has one sink (a `Writable`), one optional `Formatter` and a global level.

- A `Logger` cannot be copied or moved. Make it a static, or keep a reference to it.
- Construct and destroy loggers before other threads use `LogManager::findLogger()`. The list of loggers has no
  lock.
- The macros `LOG_TRACE()` to `LOG_ERROR()` remove a call from the binary when its level is below
  `LOG_COMPILED_MIN_LEVEL`. For example, `-DLOG_COMPILED_MIN_LEVEL=logging::Level::Info` removes the trace and debug
  calls.

## Modbus

`modbus::ModbusSlave` and `modbus::ModbusMaster` implement Modbus RTU on a `Readable` and a `Writable`, normally a
UART. The CRC is `ModbusCrc` of `utils/CrcDigest.h`.

- The slave handles Read Holding Registers (3), Read Input Registers (4) and Write Multiple Registers (16). The
  application supplies a callback for each register type.
- A write callback can call `isBroadcast()` to find out if the request has the address 0. Read callbacks do not run
  for a broadcast.
- The slave uses a buffer that the caller supplies. Its size limits the registers per request (size / 2). A full
  Modbus frame needs 250 bytes.
- Optional enable functions switch the receiver and the transmitter, for example of an RS-485 transceiver.
- On a half-duplex line (for example RS-485), the receiver also gets the reply of the slave. To remove this echo,
  construct the slave with a `BufferedReadable`. The slave then clears it after each reply. `flush()` of the
  `Writable` must return only after the last byte is sent. With a plain `Readable`, the slave does not remove the
  echo.

## Settings

A `Setting<T>` is one named value with a default, optionally in a section. Each `Setting` links itself into the
list of the `SettingsManager` when you construct it. `SettingsManager` loads and saves all settings as JSON from a
`Readable` or to a `Writable`. It uses [ArduinoJson](https://github.com/bblanchon/ArduinoJson). A consumer of the
installed package must link `ArduinoJson` itself (see [Using libArT in a CMake project](cmake-integration.md)).

The name and the section of a `Setting` are pointers. The strings must stay valid while the `Setting` exists.

## Sensors

`TemperatureSensor` is the interface. It gives the temperature and the maximum temperature in degrees Celsius.
`KTypeThermocoupleSensor` and `STypeThermocoupleSensor` convert a thermocouple voltage in µV with a lookup table.
A subclass implements `getTemperature()` with the ADC of the hardware.
`CompensatedThermocoupleSensor` adds the cold-junction temperature of a second sensor. `ThermistorSensor` reads an
NTC thermistor with a pull-up resistor. The compiler builds its ADC lookup table from the B-parameter model.

## FreeRTOS wrappers

The headers in `freertos/` (target `art::rtos`) wrap FreeRTOS objects in RAII classes: `Thread`, `Mutex`,
`RecursiveMutex`, `Semaphore`, `Queue`, `EventFlags` and `Timer`. `CriticalSection.h` has
`interrupts::critical_section`, `interrupts::isr_critical_section` and `scheduler::critical_section`. `SysClock.h`
has `sysclock`. All are in namespace `art::rtos`, and `freertos/Rtos.h` includes all of them.
The application supplies FreeRTOS and its `FreeRTOSConfig.h`, which must set `configSUPPORT_STATIC_ALLOCATION` to 1.
`RecursiveMutex` also needs `configUSE_RECURSIVE_MUTEXES` set to 1.

## Utilities

- Digests: `CrcDigest<...>`, a CRC template with the presets `ModbusCrc`, `Crc16Ccitt` and `TmcCrc8`, and
  `IBMCRC16Digest`, `IBMCRC16FastDigest` (with a lookup table) and `SHA256Digest`.
- JSON: `JSONReader` and `JSONWriter`, on ArduinoJson.
- The functions in namespace `utils::FloatText` convert between `float` and text. The result is correctly rounded
  and the shortest text that gives the same `float` back. They use [Ryu](https://github.com/ulfjack/ryu), and no
  `printf`, `strtod`, libm or heap.
- `RingBuffer`, `Timeout`, `getInterpolatedValueFromLookupTable()` (`LookupTable.h`), `endian.h`, `numberutils.h`
  and `cstring.h`.
- `utils/compat/`: `art::expected`, `art::span`, `art::bit_cast` and `art::remove_cvref_t` for C++17 and C++23. See
  [Vocabulary types](vocabulary-types.md).
