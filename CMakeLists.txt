cmake_minimum_required(VERSION 3.20)

Include(FetchContent)

# Extract git revision
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_version.cmake")

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

FetchContent_Declare(utils-lists
        GIT_REPOSITORY https://github.com/armint/utils-lists-xpack.git
        GIT_TAG d63335a397d548a48365fb2107bf59a4714f50cd
)

FetchContent_MakeAvailable(utils-lists)

FetchContent_Declare(diag-trace
        GIT_REPOSITORY https://github.com/micro-os-plus/diag-trace-xpack.git
        GIT_TAG v4.2.0
)

FetchContent_MakeAvailable(diag-trace)


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
            micro-os-plus::utils-lists
            micro-os-plus::diag-trace
            etl::etl
            ArduinoJson
)

# Ensure the exported interface does not pull non-exported targets.
set_property(TARGET art-core PROPERTY INTERFACE_LINK_LIBRARIES "")

install(TARGETS art-core
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
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/art-core)

