# Using libArT in a CMake project

This document describes the requirements of libArT, how to add it to a CMake or ESP-IDF project, its dependencies
and its CMake options. [Vocabulary types](vocabulary-types.md) describes the C++17 and C++23 modes in detail.

## Requirements

- C++17 or newer, with GCC 11 or later or Clang 9 or later (`utils/compat/bit.h` needs `__builtin_bit_cast`). CI
  builds and tests with GCC at C++17, C++20 and C++23, and with Clang at C++17 and C++23. These minimum versions are
  not tested: CI uses GCC 14.2 and Clang 20.1, with arm-none-eabi-gcc 15.3 on GitHub Actions
  (`.github/workflows/`, image `ghcr.io/armint/firmware-build-image:v1.15.0`) and 14.3 on GitLab CI (`.gitlab-ci.yml`,
  `firmware-builder:v1.14.0`).
- With `ART_USE_STD_EXPECTED_SPAN=ON` (the default unless `CMAKE_CXX_STANDARD` is below 23, see
  [CMake options](#cmake-options)), C++23:
  `art-core`, `art-rtos` and `art-storage-littlefs` then require `cxx_std_23`. With `OFF` they require
  `cxx_std_17`. Both are minimums, not pins: a superproject may build at a newer standard.
  - A build without `CMAKE_CXX_STANDARD` keeps `ON`, so art-core and its consumers build at C++23.
  - A build without `CMAKE_CXX_STANDARD` that sets `OFF` builds at the default standard of the compiler (for
    example gnu++17 for GCC 14), not at C++23.
  - A consumer that sets `-DART_USE_STD_EXPECTED_SPAN=OFF` at C++23 gets the minimum `cxx_std_17`. It still builds
    at C++23, but with the own types.
  - `ON` with a `CMAKE_CXX_STANDARD` below 23 stops the configure step with an error.
  - The ESP-IDF component build sets no compile feature and does not define the option. `utils/compat/config.h`
    then chooses from the standard library of the `-std=` flag of the project (see
    [Vocabulary types](vocabulary-types.md#art_use_std_expected_span-std-or-own-vocabulary-types)).
- With `OFF`, `art::span` is the span of [span-compat](https://github.com/armint/span-compat).
  CMake fetches span-compat only in this case. A build with `ON` does not fetch or link it. The ESP-IDF component
  is an exception: it always fetches span-compat. See
  [Vocabulary types](vocabulary-types.md#span-compat-one-span-type-per-build).

## Adding libArT to a project

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
  [Requirements](#requirements)). With `OFF`, it also carries `span-compat::span-compat`, with the definition
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
| [span-compat](https://github.com/armint/span-compat) | **PUBLIC**, only with `ART_USE_STD_EXPECTED_SPAN=OFF` | `art::span` is its span. The pin is a commit in `cmake/span-compat.cmake`. See [Vocabulary types](vocabulary-types.md#span-compat-one-span-type-per-build). |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | PRIVATE | Used by the JSON reader and writer. |
| [Ryu](https://github.com/ulfjack/ryu) | PRIVATE | Used by `FloatText`, see `cmake/ryu.cmake`. |
| [littlefs](https://github.com/littlefs-project/littlefs) | optional | Only for the `art-storage-littlefs` target (`ART_STORAGE_LITTLEFS`). |

CMake fetches all of them with `FetchContent`. To build without network access, set `FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT` to a local checkout of span-compat. The ESP-IDF
component fetches span-compat also with the `std` types (see
[Vocabulary types](vocabulary-types.md#art_use_std_expected_span-std-or-own-vocabulary-types)).

**`ETL_NO_STL` is not supported.** With `ETL_NO_STL`, ETL defines its own `std::initializer_list`, which conflicts
with the standard library headers that libArT includes.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `ART_USE_STD_EXPECTED_SPAN` | `ON`, unless `CMAKE_CXX_STANDARD` is below 23 | Selects what `art::expected`, `art::unexpected` and `art::span` are, and the minimum standard of the targets (`ON`: C++23, `OFF`: C++17). See [Vocabulary types](vocabulary-types.md). |
| `ART_STORAGE_LITTLEFS` | `OFF` | Builds the LittleFS file system binding (`art-storage-littlefs`). |
| `ART_LITTLEFS_NO_ASSERT` | `ON` when cross-compiling | Builds littlefs with `LFS_NO_ASSERT`. |
| `ART_CLANG_FORMAT` | `clang-format-20`, else `clang-format` | The clang-format executable of the `clangformat` and `clangformat-check` targets (see `cmake/clang-format.cmake`). |

