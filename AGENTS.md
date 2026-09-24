# Agent Instructions for libArT

## Purpose

`libArT` is a core utility library designed for embedded systems, providing abstractions for common tasks. Target
systems are cortex-m or risc-v microcontrollers running bare-metal or rtos-based (freertos or µOS++ IIIe).

## Core Components
- **Streams**: `Readable` and `Writable` interfaces for various data sources/sinks (Array, String, Digest, etc.).
  `FileReadable` and `FileWritable` (`src/streams/`) adapt an `art::storage::File` of any `FileSystem` to these interfaces, so `DigestReadable`, `BufferedWritable` and the codecs work on a file whatever its file system. `FileReadable` takes a caller-provided read buffer; `FileWritable` is unbuffered (wrap it in `BufferedWritable`) and reports `data.size()` on success because `File::write()` is all-or-error. `art::storage::Error` is mapped onto the existing `Readable::Error`/`Writable::Error` values (see the class comments) and the cause stays available in `lastError()`. `FatFileReadable`/`FatFileWritable` remain for `FatFile`.
- **Logging**: A flexible logging system (`LogManager`, `Logger`).
- **Modbus**: Implementations for Modbus Master and Slave.
- **Settings**: A framework for managing device settings.
- **Storage**: `art::storage` block device (`BlockDevice`) and file system (`FileSystem`, move-only `File` handle) interfaces; the FileSystem serializes its own calls, a BlockDevice does not. `LittleFsFileSystem` (`src/storage/littlefs/`) is the LittleFS implementation. It is built by the optional `art-storage-littlefs` target (`ART_STORAGE_LITTLEFS`, off by default) and is excluded from `art-core`, so `art-core` needs no littlefs. littlefs is pinned to v2.9.3 there unless the superproject already defines a `littlefs` target.
  `FatFileSystem` (FatFs, `src/storage/`) runs on a `BlockDevice` as well: its constructor takes a `BlockDevice &`, and `diskio.cpp`, the only place that knows the FAT sector size (`FF_MIN_SS`, 512 B), converts the sector numbers of FatFs into byte offsets. There is no other storage-device interface; the sector-based `StorageDevice` was removed (see the migration note below).
- **Utils**: JSON reading/writing, CRC (Digest), string utilities, and more.

## Migrating a `StorageDevice` implementation to `BlockDevice`

The sector-based `StorageDevice` interface no longer exists. A driver that implemented it (for example an SD card
over SPI) implements `art::storage::BlockDevice` (`src/storage/BlockDevice.h`) instead. All positions are byte
offsets, and results are `std::expected<void, Error>` instead of status flags and sector counts.

| `StorageDevice` | `BlockDevice` |
|---|---|
| `initialize()` returning status flags, `getStatus()` | `init()`. `FatFileSystem` remembers the outcome: a successful `init()` is "ready", a failure is `STA_NOINIT`. Return `Error::NotReady` for an empty slot. |
| `read(buffer, sector, count)`, returns the sector count | `read(sector * 512, span)`, returns `expected`. |
| `write(buffer, sector, count)` | `program(sector * 512, span)`. An SD card sets `eraseBeforeProgram = false`. |
| `getSectorCount()`, `getBlockSize()` | `geometry().size` (bytes) and `readSize`/`programSize` (512, or a divisor of 512). |
| `getEraseSize()` | `geometry().eraseSize`. An SD card reports 0 (its erase unit does not divide the capacity). |
| `erase(start, count)` | `erase(offset, length)`. With `eraseSize == 0` it succeeds and changes nothing. |
| nothing (FatFs `CTRL_SYNC` was a no-op) | `sync()`. `FatFileSystem` calls it for `CTRL_SYNC`; override it if the driver buffers writes. |

Behaviour to know when porting:

- `FatFileSystem` refuses a device (`FR_NOT_READY` on mount or format) whose geometry fails `Geometry::isValid()`,
  whose `readSize` or `programSize` does not divide 512, or whose capacity is below one sector or above 2^32 - 1
  sectors.
- `diskio.cpp` maps `Error::NotReady` and `Error::NotInitialised` to `RES_NOTRDY`, `WriteProtected` to `RES_WRPRT`,
  `InvalidArgument` to `RES_PARERR` and every other error to `RES_ERROR`. After `NotReady` or `NotInitialised` the
  drive is not ready until `init()` succeeds again, which the next `f_mount()` does. This replaces a driver
  updating its own `STA_NOINIT` flag when a card is removed.
- A `BlockDevice` is not thread-safe. Serialize access to a device that is shared with anything other than FatFs.

## Guidelines

- **Language**: C++20 (or newer). Use modern C++ features and best practices as long as they don't interfere with the embedded nature of the project (e.g., const correctness, move semantics, no c-style casts).
- **Style & Linting**: Follow the established stream patterns mechanically when adding new data handling utilities. Before completing tasks, format code using the provided CMake target: `cmake --build <build_dir> --target clangformat`.
- **Resource Management**: Use RAII for all resource management. Use `Writable` streams for formatted output in a memory-efficient way. Prefer `std::span` (or equivalent) for passing buffers to avoid size/pointer mismatches.
- **Memory**: Heap memory usage is strictly prohibited. Ensure that changes are compatible with the embedded nature of the project (low memory footprint).
- **Dependencies**: The `embedded template library` (ETL) is a dependency and should be considered before creating similar classes. Use etl classes for memory-efficient containers and algorithms.
- **Conventions**: Always use meaningful and descriptive function and variable names that reflect their purpose. Use fixed-size integer types (`uint8_t`, `uint32_t`, etc.) for performance, memory efficiency, and portability. Document any new functions or changes with clear comments. Use comments to explain complex logic or non-obvious decisions.
