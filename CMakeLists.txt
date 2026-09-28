# ---------- ESP-IDF component build ----------
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
    )
    FetchContent_GetProperties(etl)
    if(NOT etl_POPULATED)
        FetchContent_Populate(etl)
    endif()

    FetchContent_Declare(ArduinoJson
            GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
            GIT_TAG v6.21.5
    )
    FetchContent_GetProperties(ArduinoJson)
    if(NOT arduinojson_POPULATED)
        FetchContent_Populate(ArduinoJson)
    endif()

    # Ryu (https://github.com/ulfjack/ryu) backs FloatText.h/.cpp (src/utils/), which is picked up
    # by the SRC_DIRS above like any other src/utils source. A pinned tarball, not a git checkout:
    # it never changes once fetched, and a tarball+hash is verifiable offline (see
    # FETCHCONTENT_SOURCE_DIR_RYU in AGENTS.md for an offline override). Populate only: its own
    # CMakeLists.txt calls idf_component_register()/add_subdirectory() paths this project doesn't
    # want (it also builds the double conversion, which FloatText never uses); ryu_float.c
    # (src/utils/) includes the two float-only .c files it needs directly.
    FetchContent_Declare(ryu
            URL https://github.com/ulfjack/ryu/archive/4c0618b0e44f7ef027ebae05d2cc7812048f7c8f.tar.gz
            URL_HASH SHA256=5c039f2957769c5f61a8699b12d8703043b3f47c3ac6e2124c5a04afb6819f29
    )
    FetchContent_GetProperties(ryu)
    if(NOT ryu_POPULATED)
        FetchContent_Populate(ryu)
    endif()

    target_include_directories(${COMPONENT_LIB} PUBLIC
            ${etl_SOURCE_DIR}/include
            ${arduinojson_SOURCE_DIR}/src
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

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG 20.41.7
)

FetchContent_MakeAvailable(etl)

FetchContent_Declare(ArduinoJson
        GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
        GIT_TAG v6.21.5
)

FetchContent_MakeAvailable(ArduinoJson)

# Ryu (https://github.com/ulfjack/ryu, Apache-2.0 / Boost-1.0) backs FloatText.h/.cpp
# (src/utils/): correctly-rounded, shortest-round-trip float<->text conversion without newlib
# printf/strtod, libm or heap. A pinned tarball, not a git checkout: it never changes once
# fetched, and a tarball+hash is verifiable offline. Set FETCHCONTENT_SOURCE_DIR_RYU to a local
# checkout to build offline instead. Populate only, do not add_subdirectory() it: its own
# CMakeLists.txt also builds the double conversion (d2s.c and friends), which this project has no
# use for; ryu_float.c (src/utils/) includes only the two float-only .c files it needs.
FetchContent_Declare(ryu
        URL https://github.com/ulfjack/ryu/archive/4c0618b0e44f7ef027ebae05d2cc7812048f7c8f.tar.gz
        URL_HASH SHA256=5c039f2957769c5f61a8699b12d8703043b3f47c3ac6e2124c5a04afb6819f29
)
FetchContent_GetProperties(ryu)
if(NOT ryu_POPULATED)
    FetchContent_Populate(ryu)
endif()

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
if (ART_STORAGE_LITTLEFS OR NOT CMAKE_CROSSCOMPILING)
    set(_ART_NEED_LITTLEFS ON)
else ()
    set(_ART_NEED_LITTLEFS OFF)
endif ()

# Detect if we are crosscompiling, or compiling for our native system.
if (CMAKE_CROSSCOMPILING)
else ()
    add_subdirectory(tests)
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
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)


target_include_directories(art-core
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(art-core PUBLIC cxx_std_23)

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

# If a central build configuration target exists in the superproject,
# link it so this library is built with the same flags/defines.
if (TARGET art-build-config)
    # Only affect this target's compilation in the build tree; do not appear in installed export
    target_link_libraries(art-core PRIVATE $<BUILD_INTERFACE:art-build-config>)
endif ()

# Interface target for C++23 FreeRTOS wrapper headers (src/freertos/)
add_library(art-rtos INTERFACE)
add_library(art::rtos ALIAS art-rtos)
target_include_directories(art-rtos INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(art-rtos INTERFACE cxx_std_23)
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
                GIT_REPOSITORY https://github.com/littlefs-project/littlefs.git
                # Tag v2.11.3. The commit is pinned because a tag can be moved.
                GIT_TAG 6cb4e86540eca0d9ba62500a298385c9d863c8be
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
    target_compile_features(art-storage-littlefs PUBLIC cxx_std_23)
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

install(TARGETS art-core art-rtos
        EXPORT art-core-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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

