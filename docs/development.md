# Development

This document is for contributors to libArT. It describes how to build and run the tests, and how to format the
sources.

## Tests

The tests use [doctest](https://github.com/doctest/doctest). A native build (not a cross-compiled one) adds the
`tests/` directory. To build and run the main test binary:

```sh
cmake -B build -D CMAKE_BUILD_TYPE=Debug
cmake --build build --target libArT-tests -j
./build/tests/libArT-tests
```

A native build always builds littlefs for the tests, also with `ART_STORAGE_LITTLEFS=OFF`. Most storage and LittleFS
tests use C++20, so a build with `CMAKE_CXX_STANDARD` below 20 leaves them out. Three other binaries run the same
tests in other configurations:

- `libArT-tests-64bit` defines `LIBART_ENABLE_64BIT` (64-bit `big_int_t` and `big_uint_t`, see `src/art_types.h`).
- `libArT-tests-float` defines `PRINTF_ENABLE_FLOATING_POINT` (the `%f` conversion of `tfp_printf`).
- `libArT-tests-float-64bit` defines both.

`libArT-tests` uses neither macro, because the shipped `art-core` does not use them. The CI job `test` builds and runs
all four with GCC at C++17, C++20 and C++23, and with Clang at C++17 and C++23 (see `.gitlab-ci.yml`).

Two compat binaries have a fixed standard: `libArT-compat-tests-cpp17` (C++17) and `libArT-compat-tests-cpp23-own`
(C++23 with the own types). A build has them only when it fetches span-compat. The C++23 cells of the CI job
`test` build and run them.
The scripts `tests/check-install.sh` and `tests/check-subproject.sh` check the installed package and the use as a
subproject (see [Using libArT in a CMake project](cmake-integration.md)).

## Formatting

The sources in `src/` and `tests/` are formatted with clang-format 20 and the `.clang-format` of the repository.
Third-party sources are listed in `.clang-format-ignore` and keep the format of their upstream.

- `cmake --build <build_dir> --target clangformat` formats all files in place.
- `cmake --build <build_dir> --target clangformat-check` changes nothing and fails if a file is not formatted. The CI
  job `format` runs it.
- `git clang-format-20 origin/main` formats only the lines that you changed.

The targets exist only when CMake finds clang-format 18 or later and libArT is the top-level project (see
`cmake/clang-format.cmake`). Other major versions than 20 can format differently.

The whole tree was reformatted in one commit. To make `git blame` skip that commit, run once per clone:

```sh
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

