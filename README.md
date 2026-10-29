# libArT

libArT is a C++ utility library for embedded systems. It targets Cortex-M and RISC-V microcontrollers, bare-metal
or with FreeRTOS. It uses no heap, and its code compiles at C++17 and at C++23.

It contains:

- **Streams**: `Readable` and `Writable` interfaces, with implementations for memory, files, digests, hex and
  buffers, and `printf()` without a heap.
- **Logging**: loggers with topics and levels, and log calls that the compiler can remove.
- **Modbus**: Modbus RTU master and slave.
- **Settings**: device settings with defaults, stored as JSON.
- **Storage**: block device and file system interfaces, with FatFs and LittleFS file systems.
- **USB**: a USB full-speed device stack with CDC-ACM (virtual COM port), a vendor bulk function and MS OS 2.0
  descriptors for WinUSB.
- **Sensors**: thermocouple and thermistor temperature sensors.
- **FreeRTOS wrappers**: RAII classes for threads, mutexes, queues, semaphores and timers.
- **Utilities**: CRC and SHA-256, JSON, float-to-text conversion, ring buffers and more.

libArT has no hardware code. A port for an MCU family, for example
[libart-stm32](https://gitlab.vandertogt.org/embedded-libraries/libart-stm32), connects it to the peripherals.

## Usage

Add libArT to a CMake project, for example as a git submodule in `dependencies/libart`. Then add the directory and
link `art::core`:

```cmake
add_subdirectory(dependencies/libart)
target_link_libraries(firmware PRIVATE art::core)
```

Link `art::rtos` for the FreeRTOS wrappers, and `art::storage-littlefs` (with `ART_STORAGE_LITTLEFS=ON`) for the
LittleFS file system. libArT is also an ESP-IDF component, and it can be installed as the CMake package `art-core`.

Include the headers with their path below `src/`:

```cpp
#include "logging/LogManager.h"
#include "logging/Logger.h"
#include "streams/StringWritable.h"

static logging::Logger logger("app");

void report(Writable &uart, int temperature) {
   char text[32];
   StringWritable out(text);
   out.printf("T=%d C", temperature);

   LogManager::getInstance().set_sink(&uart);
   logger.info("%s", text);
}
```

## Documentation

- [Components](docs/components.md): an overview of each component.
- [Using libArT in a CMake project](docs/cmake-integration.md): requirements, `add_subdirectory()`, ESP-IDF, the
  installed package, dependencies and CMake options.
- [Vocabulary types](docs/vocabulary-types.md): `art::expected` and `art::span` at C++17 and C++23, and
  `ART_USE_STD_EXPECTED_SPAN`.
- [Storage](docs/storage.md): block devices, FatFs, LittleFS and its boot sequence.
- [USB device stack](docs/usb-device-stack.md): `usb::Device`, CDC-ACM, vendor bulk functions, descriptors and the
  controller contract.
- [Development](docs/development.md): tests and formatting.

## Contributing

The repository has one branch: `main`. All work, bug fixes too, goes to `main` through a pull request on
[GitHub](https://github.com/armint/libart). Use a tag or a commit of `main` in your submodule.

## License

libArT is licensed under the [MIT License](LICENSE). Some files in `src/` are third-party code under their own
licenses:

- `src/storage/fatfs/`: FatFs, under the FatFs license (`src/storage/fatfs/LICENSE.txt`).
- `src/utils/lists.h` and `src/utils/lists.cpp`: from µOS++, MIT License.
- `src/utils/tfp_printf.h` and `src/utils/tfp_printf.cpp`: from Kustaa Nyholm / SpareTimeLabs, BSD license.

CMake fetches the dependencies (ETL, ArduinoJson, Ryu, span-compat and littlefs). They have their own licenses. Ryu
is used under the Boost Software License 1.0.
