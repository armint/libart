# ---------- ESP-IDF component build ----------
# ETL is exported PUBLIC below (libArT headers use ETL containers). There is no CMAKE_CXX_STANDARD here, so
# ART_USE_STD_EXPECTED_SPAN is not defined and utils/compat/config.h derives it from the language standard.
if(ESP_PLATFORM)
    idf_component_register(
            SRC_DIRS "src" "src/logging" "src/modbus" "src/settings" "src/streams" "src/utils" "src/storage" "src/storage/fatfs"
            INCLUDE_DIRS "src"
    )

    # Fetch header-only dependencies without processing their CMakeLists.txt,
    # because they call idf_component_register() themselves which conflicts
    # with our component registration above.
    Include(FetchContent)

    FetchContent_Declare(
            etl
            GIT_REPOSITORY https://github.com/ETLCPP/etl.git
            GIT_TAG 20.41.7
            GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(etl)
    if(NOT etl_POPULATED)
        FetchContent_Populate(etl)
    endif()

    FetchContent_Declare(ArduinoJson
            GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
            GIT_TAG v6.21.5
            GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(ArduinoJson)
    if(NOT arduinojson_POPULATED)
        FetchContent_Populate(ArduinoJson)
    endif()

    # The span of span-compat is art::span when the standard has no std::expected (utils/compat/span.h). There is no
    # SPAN_COMPAT_USE_STD_SPAN definition either, so a translation unit at C++20 stops with an #error there: build
    # the component at C++17 or C++23.
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/span-compat.cmake")
    FetchContent_Declare(
            span_compat
            GIT_REPOSITORY ${ART_SPAN_COMPAT_GIT_REPOSITORY}
            GIT_TAG ${ART_SPAN_COMPAT_GIT_TAG}
    )
    FetchContent_GetProperties(span_compat)
    if(NOT span_compat_POPULATED)
        FetchContent_Populate(span_compat)
    endif()

    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ryu.cmake")

    target_include_directories(${COMPONENT_LIB} PUBLIC
            ${etl_SOURCE_DIR}/include
            ${arduinojson_SOURCE_DIR}/src
            ${span_compat_SOURCE_DIR}/include
    )
    # Private: only ryu_float.c and FloatText.cpp include ryu/*, never a public libArT header.
    target_include_directories(${COMPONENT_LIB} PRIVATE ${ryu_SOURCE_DIR})

    return()
endif()

# ---------- Standalone CMake build (STM32, native tests, etc.) ----------
cmake_minimum_required(VERSION 3.20)

Include(FetchContent)

# Extract git revision
include("${CMAKE_CURRENT_LIST_DIR}/cmake/generate_version.cmake")

project(art-core VERSION 0.1.0 LANGUAGES C CXX ASM)

include(GNUInstallDirs)

# The littlefs tarball below: give the extracted files the time of the extraction (CMake >= 3.24 warns otherwise). The
# same setting in cmake/ryu.cmake does not reach this file, because include() opens its own policy scope.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# The git dependencies are pinned by tag and cloned shallow (GIT_SHALLOW): only the tagged commit is fetched, not the
# history (about 40 MB for ETL).
FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG 20.41.7
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(etl)

FetchContent_Declare(ArduinoJson
        GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
        GIT_TAG v6.21.5
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(ArduinoJson)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/ryu.cmake")

# The LittleFS binding (art-storage-littlefs) is optional and off by default, so that art-core and every
# existing consumer stay free of a littlefs dependency (and of its fetch). The native unit tests compile the
# binding and littlefs themselves, so they do not depend on this option, but they still need the littlefs
# sources: _ART_NEED_LITTLEFS makes the littlefs target exist for them.
option(ART_STORAGE_LITTLEFS "Build the LittleFS FileSystem binding (art-storage-littlefs)" OFF)
# littlefs asserts on misuse of its API. That is what a host test wants; firmware must not abort a boot on a
# file system error (the binding validates its inputs instead), so a cross build defaults to no asserts.
if (CMAKE_CROSSCOMPILING)
    set(_ART_LITTLEFS_NO_ASSERT_DEFAULT ON)
else ()
    set(_ART_LITTLEFS_NO_ASSERT_DEFAULT OFF)
endif ()
option(ART_LITTLEFS_NO_ASSERT "Build littlefs with LFS_NO_ASSERT" ${_ART_LITTLEFS_NO_ASSERT_DEFAULT})

# art::expected, art::unexpected and art::span (src/utils/compat/) alias the std types when this is ON, and are
# libArT's own expected and the own span of span-compat when it is OFF. The choice is exported as a PUBLIC compile definition, so that
# libArT, libArT-proto and the application agree on it: Readable/Writable have virtual functions that take and return
# these types, and mixing both in one program is a silent ODR violation. Default: ON unless the superproject asks
# for a standard below C++23. The minimum standard of art-core, art-rtos and art-storage-littlefs follows the choice:
# C++23 with the std types (std::expected needs it), C++17 with the own types (see _ART_MIN_CXX_STD below).
if (DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD LESS 23)
    set(_ART_USE_STD_EXPECTED_SPAN_DEFAULT OFF)
else ()
    set(_ART_USE_STD_EXPECTED_SPAN_DEFAULT ON)
endif ()
option(ART_USE_STD_EXPECTED_SPAN "Use std::expected/std::span for art::expected/art::span (needs C++23 everywhere)"
        ${_ART_USE_STD_EXPECTED_SPAN_DEFAULT})
if (ART_USE_STD_EXPECTED_SPAN)
    set(_ART_USE_STD_EXPECTED_SPAN_VALUE 1)
    set(_ART_MIN_CXX_STD cxx_std_23)
else ()
    set(_ART_USE_STD_EXPECTED_SPAN_VALUE 0)
    set(_ART_MIN_CXX_STD cxx_std_17)
endif ()
# A mixed configuration stops here instead of in the first translation unit (utils/compat/config.h).
if (ART_USE_STD_EXPECTED_SPAN AND DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "ART_USE_STD_EXPECTED_SPAN=ON needs C++23, but CMAKE_CXX_STANDARD is ${CMAKE_CXX_STANDARD}. "
            "Configure with -DART_USE_STD_EXPECTED_SPAN=OFF, or with -DCMAKE_CXX_STANDARD=23.")
endif ()

# With ART_USE_STD_EXPECTED_SPAN=OFF, art::span is the span of span-compat (utils/compat/span.h), the span type that
# system-stm32 uses. One span type per build: span-compat must give its own span to every target, so libArT sets its
# option SPAN_COMPAT_USE_STD_SPAN to OFF. A superproject that adds span-compat itself must set the option to OFF
# before it adds span-compat or libArT, or find_package() a span-compat package that was installed with OFF. With ON,
# span-compat is not fetched or linked, and a span-compat in the same build must be ON, so that span_compat::span is
# std::span in every target (checked at the end of the configure). AUTO is not enough: it gives the own span to a
# target below C++20, for example system-stm32 at C++17. The pin is in cmake/span-compat.cmake.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/span-compat.cmake")
# Sets <out> to the span type of span-compat::span-compat: NONE (no target), OFF, ON or AUTO. It reads the definition
# on the target, so the alias of add_subdirectory() and the imported target of find_package() give the same result.
function(_art_span_compat_type out)
    if (NOT TARGET span-compat::span-compat)
        set(${out} NONE PARENT_SCOPE)
        return()
    endif ()
    get_target_property(_definitions span-compat::span-compat INTERFACE_COMPILE_DEFINITIONS)
    if ("SPAN_COMPAT_USE_STD_SPAN=0" IN_LIST _definitions)
        set(${out} OFF PARENT_SCOPE)
    elseif ("SPAN_COMPAT_USE_STD_SPAN=1" IN_LIST _definitions)
        set(${out} ON PARENT_SCOPE)
    else ()
        set(${out} AUTO PARENT_SCOPE)
    endif ()
endfunction()
# expected: OFF (ART_USE_STD_EXPECTED_SPAN=OFF) or ON. Without a target, OFF checks the cache option, which decides
# the type of the span-compat that libArT adds. An undefined option passes: libArT sets it.
function(_art_check_span_compat expected)
    _art_span_compat_type(_type)
    if (_type STREQUAL "NONE" AND DEFINED SPAN_COMPAT_USE_STD_SPAN)
        set(_value "${SPAN_COMPAT_USE_STD_SPAN}")
    else ()
        set(_value "${_type}")
    endif ()
    if (expected STREQUAL "OFF" AND NOT _value MATCHES "^(NONE|OFF)$")
        message(FATAL_ERROR "ART_USE_STD_EXPECTED_SPAN=OFF needs SPAN_COMPAT_USE_STD_SPAN=OFF, but it is '${_value}'. "
                "Set -DSPAN_COMPAT_USE_STD_SPAN=OFF before span-compat is added, or use a span-compat package that "
                "was installed with OFF.")
    elseif (expected STREQUAL "ON" AND NOT _type MATCHES "^(NONE|ON)$")
        message(FATAL_ERROR "ART_USE_STD_EXPECTED_SPAN=ON makes art::span std::span, but span-compat is configured "
                "with SPAN_COMPAT_USE_STD_SPAN=${_type}, which gives the own span to some or all targets. Use ON, or "
                "ART_USE_STD_EXPECTED_SPAN=OFF.")
    endif ()
endfunction()
if (NOT ART_USE_STD_EXPECTED_SPAN)
    _art_check_span_compat(OFF)
    set(SPAN_COMPAT_USE_STD_SPAN OFF CACHE STRING "Span type: AUTO (by __cpp_lib_span), ON (std::span) or OFF (own)")
    # Install the span-compat package with libArT, as ArduinoJson: the installed art::core needs it.
    if (NOT DEFINED SPAN_COMPAT_INSTALL)
        set(SPAN_COMPAT_INSTALL ON)
    endif ()
    # A superproject may have added span-compat already: add_subdirectory(), FetchContent or find_package().
    if (NOT TARGET span-compat::span-compat)
        FetchContent_Declare(
                span_compat
                GIT_REPOSITORY ${ART_SPAN_COMPAT_GIT_REPOSITORY}
                GIT_TAG ${ART_SPAN_COMPAT_GIT_TAG}
        )
        FetchContent_MakeAvailable(span_compat)
    endif ()
    _art_check_span_compat(OFF)
else ()
    cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _art_check_span_compat ON)
endif ()
if (ART_STORAGE_LITTLEFS OR NOT CMAKE_CROSSCOMPILING)
    set(_ART_NEED_LITTLEFS ON)
else ()
    set(_ART_NEED_LITTLEFS OFF)
endif ()

# The compat test binaries (tests/CMakeLists.txt) need span-compat also with ART_USE_STD_EXPECTED_SPAN=ON. They are
# built only when libArT is the top-level project, so that a C++23 superproject does not fetch span-compat.
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_ART_IS_TOP_LEVEL ON)
else ()
    set(_ART_IS_TOP_LEVEL OFF)
endif ()

# Detect if we are crosscompiling, or compiling for our native system.
if (CMAKE_CROSSCOMPILING)
else ()
    add_subdirectory(tests)
endif ()

# clangformat and clangformat-check (see cmake/clang-format.cmake). Not for a superproject, which may have its own.
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/clang-format.cmake")
endif ()

file(GLOB_RECURSE PROJECT_SOURCES
        "${PROJECT_SOURCE_DIR}/src/*.c"
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
)
# The LittleFS binding needs littlefs, so it must not be swept into art-core; art-storage-littlefs builds it.
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "/src/storage/littlefs/")

file(GLOB_RECURSE PROJECT_HEADERS
        "src/*.h"
        "src/*.hpp"
)

add_library(art-core STATIC ${PROJECT_SOURCES})

add_library(art::core ALIAS art-core)

target_include_directories(
        art-core
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/libart>)

target_compile_features(art-core PUBLIC ${_ART_MIN_CXX_STD})
target_compile_definitions(art-core PUBLIC ART_USE_STD_EXPECTED_SPAN=${_ART_USE_STD_EXPECTED_SPAN_VALUE})

# Private: only ryu_float.c and FloatText.cpp include ryu/*, never a public libArT header.
target_include_directories(art-core PRIVATE $<BUILD_INTERFACE:${ryu_SOURCE_DIR}>)

# Link dependencies privately to avoid exporting build-tree targets via
# our install(EXPORT ...) set. Consumers will get dependencies re-attached
# in the installed package config.
target_link_libraries(art-core
        PRIVATE
        etl::etl
        ArduinoJson
)
if (NOT ART_USE_STD_EXPECTED_SPAN)
    target_link_libraries(art-core PRIVATE span-compat::span-compat)
endif ()

# If a central build configuration target exists in the superproject,
# link it so this library is built with the same flags/defines.
if (TARGET art-build-config)
    # Only affect this target's compilation in the build tree; do not appear in installed export
    target_link_libraries(art-core PRIVATE $<BUILD_INTERFACE:art-build-config>)
endif ()

# Interface target for the FreeRTOS wrapper headers (src/freertos/)
add_library(art-rtos INTERFACE)
add_library(art::rtos ALIAS art-rtos)
target_include_directories(art-rtos INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/libart>
)
target_compile_features(art-rtos INTERFACE ${_ART_MIN_CXX_STD})
target_compile_definitions(art-rtos INTERFACE ART_USE_STD_EXPECTED_SPAN=${_ART_USE_STD_EXPECTED_SPAN_VALUE})
# Build tree only, as ETL for art-core below: the installed art-coreConfig.cmake links the installed span-compat.
if (NOT ART_USE_STD_EXPECTED_SPAN)
    target_link_libraries(art-rtos INTERFACE $<BUILD_INTERFACE:span-compat::span-compat>)
endif ()
if (TARGET freertos)
    target_link_libraries(art-rtos INTERFACE $<BUILD_INTERFACE:freertos>)
elseif (TARGET freertos_kernel)
    target_link_libraries(art-rtos INTERFACE $<BUILD_INTERFACE:freertos_kernel>)
endif()
if (TARGET art-build-config)
    target_link_libraries(art-rtos INTERFACE $<BUILD_INTERFACE:art-build-config>)
endif()

# ---------- LittleFS binding ----------
if (_ART_NEED_LITTLEFS)
    # littlefs ships no usable CMakeLists.txt, so the target is defined here from lfs.c and lfs_util.c. A
    # superproject that already defines a "littlefs" target wins, and then owns the version pin.
    if (NOT TARGET littlefs)
        # The pin is firmware visible: a bump changes the code that runs on every node that uses it. Bump it
        # in its own change, with a mount test against a volume written by the previous version
        # (tests/storage/TestLittleFsCompat.cpp). The binding reads littlefs internals (see
        # LittleFsFileSystem.cpp, dropReadCaches()), so a tag is used instead of a branch and every bump is
        # reviewed against lfs.c. LittleFsFileSystem.cpp refuses versions other than the reviewed v2.9 and
        # v2.11, which also covers a superproject that defines its own target. Re-evaluate when a new 2.x
        # release appears; v3 changes the on-disk format and is out of scope.
        FetchContent_Declare(
                littlefs
                # Tag v2.11.3. The commit is pinned because a tag can be moved. A tarball of that commit instead of a
                # git clone: a clone of a commit cannot be shallow, and the hash makes the download verifiable.
                URL https://github.com/littlefs-project/littlefs/archive/6cb4e86540eca0d9ba62500a298385c9d863c8be.tar.gz
                URL_HASH SHA256=4803faf6db6c60093558a315a10c50926d2cfa5668a3f03fa2523fd09b10b747
                # Populate only: do not add_subdirectory() whatever CMakeLists.txt upstream ships.
                SOURCE_SUBDIR art-no-cmake
        )
        FetchContent_MakeAvailable(littlefs)

        add_library(littlefs STATIC
                ${littlefs_SOURCE_DIR}/lfs.c
                ${littlefs_SOURCE_DIR}/lfs_util.c
        )
        target_include_directories(littlefs PUBLIC $<BUILD_INTERFACE:${littlefs_SOURCE_DIR}>)
        # LFS_NO_MALLOC:  the binding supplies every buffer statically.
        # LFS_NO_ASSERT:  a file system error must never abort a boot; the binding validates instead. Only
        #                 set when ART_LITTLEFS_NO_ASSERT is on, which is the default for cross builds.
        # LFS_NO_DEBUG/WARN/ERROR: littlefs traces go to printf, which these projects forbid; the binding logs
        #                 through logging::Logger.
        target_compile_definitions(littlefs PUBLIC
                LFS_NO_MALLOC
                LFS_NO_DEBUG
                LFS_NO_WARN
                LFS_NO_ERROR
        )
        if (ART_LITTLEFS_NO_ASSERT)
            target_compile_definitions(littlefs PUBLIC LFS_NO_ASSERT)
        endif ()
        if (TARGET art-build-config)
            target_link_libraries(littlefs PRIVATE $<BUILD_INTERFACE:art-build-config>)
        endif ()
    endif ()
endif ()

if (ART_STORAGE_LITTLEFS)
    add_library(art-storage-littlefs STATIC src/storage/littlefs/LittleFsFileSystem.cpp)
    add_library(art::storage-littlefs ALIAS art-storage-littlefs)
    target_compile_features(art-storage-littlefs PUBLIC ${_ART_MIN_CXX_STD})
    # art-core provides FileSystem, File and the logger; art-rtos the mutex; littlefs is exported PUBLIC
    # because the template header includes lfs.h.
    target_link_libraries(art-storage-littlefs PUBLIC art-core art-rtos littlefs)
    target_link_libraries(art-storage-littlefs PRIVATE etl::etl)
    # A native build has no FreeRTOS target. Compile against the host stubs of the unit tests so that the
    # target builds there (CI does); a cross build gets FreeRTOS from the superproject through art-rtos.
    if (NOT CMAKE_CROSSCOMPILING AND NOT TARGET freertos AND NOT TARGET freertos_kernel)
        target_include_directories(art-storage-littlefs PRIVATE
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs/freertos>)
    endif ()
    if (TARGET art-build-config)
        target_link_libraries(art-storage-littlefs PRIVATE $<BUILD_INTERFACE:art-build-config>)
    endif ()
    # Not part of install(EXPORT): it links littlefs, which is not exported. Consumers use add_subdirectory().
endif ()

# Ensure the exported interface does not pull non-exported targets.
set_property(TARGET art-core PROPERTY INTERFACE_LINK_LIBRARIES "")
# ETL is a PUBLIC dependency: public headers use ETL (for example storage/FileSystem.h). It is added after the reset above
# and only for the build tree: the etl target of FetchContent is not in an export set, and CMake refuses to export a
# link to it (also inside $<INSTALL_INTERFACE:...>). The installed art-coreConfig.cmake finds the installed ETL package
# (find_dependency(etl)) and links etl::etl to art::core itself.
target_link_libraries(art-core INTERFACE $<BUILD_INTERFACE:etl::etl>)
# span-compat is a PUBLIC dependency with ART_USE_STD_EXPECTED_SPAN=OFF (utils/compat/span.h), for the same reason
# linked for the build tree only. The link also gives every consumer the SPAN_COMPAT_USE_STD_SPAN=0 definition.
if (NOT ART_USE_STD_EXPECTED_SPAN)
    target_link_libraries(art-core INTERFACE $<BUILD_INTERFACE:span-compat::span-compat>)
endif ()

# Export the targets as art::core and art::rtos, the names of the aliases that add_subdirectory() consumers link.
set_target_properties(art-core PROPERTIES EXPORT_NAME core)
set_target_properties(art-rtos PROPERTIES EXPORT_NAME rtos)
install(TARGETS art-core art-rtos
        EXPORT art-core-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# The public headers keep their path below src/, so that the includes of a consumer (for example
# "streams/Readable.h") are the same as with add_subdirectory(). They go to include/libart/, the include directory of
# the exported targets, so that generic names such as utils/ or art_types.h do not collide with the headers of other
# packages in a shared prefix. Left out: storage/littlefs/, because
# art-storage-littlefs is not exported and its header includes lfs.h. freertos/ is installed for art::rtos; the
# consumer provides FreeRTOS. Ryu is only included by sources, never by a header.
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libart
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
        PATTERN "storage/littlefs" EXCLUDE)

install(EXPORT art-core-targets
        NAMESPACE art::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/art-core)

# Generate a Config file so consumers can `find_package(art-core)`
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/art-coreConfigVersion.cmake
        VERSION ${GIT_VERSION}
        COMPATIBILITY SameMajorVersion)

configure_package_config_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/art-coreConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/art-coreConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/art-core)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/art-coreConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/art-coreConfigVersion.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/BuildConfig.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/output_files.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/generate_version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/art-core)

