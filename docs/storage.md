# Storage

`src/storage/` (namespace `art::storage`) has two interfaces:

- `BlockDevice` (`storage/BlockDevice.h`) is the medium, for example NOR flash or an SD card. All positions are byte
  offsets. `geometry()` gives the size, the read, program and erase units, and if the medium needs an erase before a
  program.
- `FileSystem` (`storage/FileSystem.h`) is a file system on a `BlockDevice`. `open()` returns a move-only `File`
  handle.

All calls return `art::expected<..., Error>`. `Error` is in `storage/StorageError.h`.

## Thread safety

A `FileSystem` serializes its own calls. Several tasks can call it at the same time, and each single call is
atomic. `readFile()` and `writeFile()` are made of several calls, so they are not atomic as a unit. To replace a
file atomically, write a temporary file and `rename()` it over the target.

A `BlockDevice` does not serialize. Serialize access to a device that is shared with anything other than its file
system.

A `FileSystem` cannot be copied or moved, because each `File` points to it. It must exist longer than all its files.

## FatFs: `FatFileSystem`

`FatFileSystem` uses [FatFs](http://elm-chan.org/fsw/ff/) (in `src/storage/fatfs/`) on a `BlockDevice &`. It is
part of `art-core`. `diskio.cpp` converts the FatFs sectors of 512 bytes into byte offsets.

- `FatFileSystem` refuses a device (`FR_NOT_READY` on mount or format) in these cases:
  - its geometry fails `Geometry::isValid()`;
  - its `readSize` or `programSize` does not divide 512;
  - its capacity is less than one sector, or more than 2^32 - 1 sectors.
- `diskio.cpp` maps `Error::NotReady` and `Error::NotInitialised` to `RES_NOTRDY`, `WriteProtected` to `RES_WRPRT`,
  `InvalidArgument` to `RES_PARERR` and all other errors to `RES_ERROR`.
- After `NotReady` or `NotInitialised`, the drive is not ready until `init()` succeeds again. The next `f_mount()`
  calls `init()`. A driver must report a removed SD card with
  `Error::NotReady`.

## LittleFS: `LittleFsFileSystem`

`LittleFsFileSystem` (`src/storage/littlefs/`) uses [littlefs](https://github.com/littlefs-project/littlefs). It
is in the optional target `art::storage-littlefs`. Set `ART_STORAGE_LITTLEFS=ON` to build it. It is not part of
`art-core` and not part of the installed package.

- CMake fetches littlefs v2.11.3, pinned by commit and SHA-256. A superproject that defines its own `littlefs`
  target sets the version. The binding accepts only the reviewed versions v2.9 and v2.11 (a `static_assert` on
  `LFS_VERSION`). Both use the same on-disk format.
- `mount()` and `format()` refuse a device without an erase unit, and a geometry that does not fit the cache size.
  They then return `Error::InvalidArgument`. See "Geometry" in `LittleFsFileSystem.h`.
- If littlefs reports the volume as corrupt, `mount()` formats the device once and mounts again. A blank chip and a
  chip with another file system look corrupt. **Thus `mount()` erases the data of a corrupt volume, also when a
  repair is possible.** Other errors, for example `Io`, do not cause a format.

### Boot sequence

After a remount, or after an unclean power-down, littlefs can hide one directory entry. This is the entry that
follows the moved entry of a `rename()`, in the directory that the rename moved it from.
`visitDirectory()` misses it, and `stat()`, `open()` for read and `readFile()` report `NoEntry` for it.
littlefs repairs this only in its first call that changes the volume. The cause is an upstream regression
(littlefs commit ddbfcaa7, in v2.9.1 to v2.11.3).

Thus, once per boot:

1. Call `mount()`.
2. Call `makeConsistent()` before you list or stat anything that is important.
3. If `makeConsistent()` fails, call `unmount()` and `mount()`. Then continue read-only without the repair.

Do not call `makeConsistent()` again in a loop. On a consistent volume it reads and writes nothing.

### Poisoned mount

Some failures leave the state of littlefs unknown. After such a failure, the mount is poisoned: every call fails
with `Error::NeedsRemount` until `unmount()` and `mount()`. These failures poison the mount:

- a failed `rename()`;
- a failed `makeConsistent()`;
- a failed change while another file is open for write.

`unmount()` of a poisoned mount commits nothing. It drops the data of open writers that is not synced. See
"Poisoned mount" and "Consistency after a remount" in `LittleFsFileSystem.h` for the details.

## Streams on files

`FileReadable` and `FileWritable` (`src/streams/`) adapt a `File` of any `FileSystem` to `Readable` and `Writable`.
`FileReadable` uses a read buffer that the caller supplies. `FileWritable` has no buffer, so wrap it in a
`BufferedWritable`. `lastError()` gives the `art::storage::Error` of the last failure.

## Migrating a `StorageDevice` driver

The sector-based `StorageDevice` interface no longer exists. A driver of it, for example for an SD card on SPI,
implements `BlockDevice` instead:

| `StorageDevice` | `BlockDevice` |
|---|---|
| `initialize()` with status flags, `getStatus()` | `init()`. Return `Error::NotReady` for an empty slot. `FatFileSystem` keeps the result. |
| `read(buffer, sector, count)` | `read(sector * 512, span)` |
| `write(buffer, sector, count)` | `program(sector * 512, span)`. An SD card sets `eraseBeforeProgram = false`. |
| `getSectorCount()`, `getBlockSize()` | `geometry().size` (bytes), `readSize` and `programSize` (512, or a divisor of 512). |
| `getEraseSize()` | `geometry().eraseSize`. An SD card reports 0, because its erase unit does not divide the capacity. |
| `erase(start, count)` | `erase(offset, length)`. With `eraseSize == 0`, it succeeds and changes nothing. |
| none (FatFs `CTRL_SYNC` did nothing) | `sync()`. `FatFileSystem` calls it for `CTRL_SYNC`. Override it if the driver buffers writes. |
