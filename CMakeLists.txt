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

    target_include_directories(${COMPONENT_LIB} PUBLIC
            ${etl_SOURCE_DIR}/include
            ${arduinojson_SOURCE_DIR}/src
    )

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

# Detect if we are crosscompiling, or compiling for our native system.
if (CMAKE_CROSSCOMPILING)
else ()
    add_subdirectory(tests)
endif ()

file(GLOB_RECURSE PROJECT_SOURCES
        "${PROJECT_SOURCE_DIR}/src/*.c"
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

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

# Interface target for C++23 FreeRTOS wrapper headers (src/rtos/)
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

