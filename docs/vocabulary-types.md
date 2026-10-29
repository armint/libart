# Vocabulary types: `art::expected` and `art::span`

libArT compiles at C++17 and at C++23. This document describes how the CMake option `ART_USE_STD_EXPECTED_SPAN`
selects the vocabulary types of the public interface, and why one build must use one choice everywhere.

## `ART_USE_STD_EXPECTED_SPAN`: `std` or own vocabulary types

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

## span-compat: one span type per build

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

