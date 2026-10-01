# libArT

Core utility library for embedded systems (Cortex-M and RISC-V microcontrollers, bare-metal or with an RTOS):
streams, logging, Modbus, settings, storage and utilities. `AGENTS.md` describes the components in detail.

## Requirements

- C++17 or newer, with GCC 11 or later or Clang 9 or later (`utils/compat/bit.h` needs `__builtin_bit_cast`). CI
  builds and tests with GCC at C++17, C++20 and C++23, and with Clang at C++17 and C++23.
- With `ART_USE_STD_EXPECTED_SPAN=ON` (the default unless `CMAKE_CXX_STANDARD` is below 23, see below), C++23:
  `art-core`, `art-rtos` and `art-storage-littlefs` then require `cxx_std_23`. With `OFF` they require
  `cxx_std_17`. Both are minimums, not pins: a superproject may build at a newer standard.
  - A build without `CMAKE_CXX_STANDARD` keeps `ON`, so art-core and its consumers build at C++23.
  - A consumer that sets `-DART_USE_STD_EXPECTED_SPAN=OFF` at C++23 gets the minimum `cxx_std_17`. It still builds
    at C++23, but with the own types.
  - The ESP-IDF component build sets no compile feature and does not define the option. `utils/compat/config.h`
    then chooses from the standard library of the `-std=` flag of the project (see below).

## Using libArT in a CMake project

Add the repository with `add_subdirectory()` and link `art::core` (and `art::rtos` for the FreeRTOS wrappers).
ESP-IDF projects use the repository as a component.

## Dependencies

| Dependency | Linked | Notes |
|---|---|---|
| [ETL](https://github.com/ETLCPP/etl) (embedded template library) | **PUBLIC** | Public headers use ETL types, for example `art::span` (see below). With `add_subdirectory()`, a target that links `art::core` gets the ETL include path; the ESP-IDF component exports it as well. The installed package (`find_package(art-core)`) only finds ETL; the consumer must link `etl::etl` itself. |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | PRIVATE | Used by the JSON reader and writer. |
| [Ryu](https://github.com/ulfjack/ryu) | PRIVATE | Used by `FloatText`, see `cmake/ryu.cmake`. |
| [littlefs](https://github.com/littlefs-project/littlefs) | optional | Only for the `art-storage-littlefs` target (`ART_STORAGE_LITTLEFS`). |

CMake fetches all of them with `FetchContent`.

**`ETL_NO_STL` is not supported.** With `ETL_NO_STL`, ETL defines its own `std::initializer_list`, which conflicts
with the standard library headers that libArT includes.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `ART_USE_STD_EXPECTED_SPAN` | `ON`, unless `CMAKE_CXX_STANDARD` is below 23 | Selects what `art::expected`, `art::unexpected` and `art::span` are, and the minimum standard of the targets (`ON`: C++23, `OFF`: C++17). See below. |
| `ART_STORAGE_LITTLEFS` | `OFF` | Builds the LittleFS file system binding (`art-storage-littlefs`). |
| `ART_LITTLEFS_NO_ASSERT` | `ON` when cross-compiling | Builds littlefs with `LFS_NO_ASSERT`. |

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
