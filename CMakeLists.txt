cmake_minimum_required(VERSION 3.20)

Include(FetchContent)

project(libArT LANGUAGES C CXX ASM)

include(GNUInstallDirs)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG 20.37.0
)

FetchContent_MakeAvailable(etl)

FetchContent_Declare(ArduinoJson
        GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
        GIT_TAG v6.21.3
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

add_library(libArT STATIC ${PROJECT_SOURCES})

target_include_directories(
        libArT
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(libArT PUBLIC cxx_std_23)

target_link_libraries(libArT PUBLIC micro-os-plus::utils-lists)
target_link_libraries(libArT PUBLIC micro-os-plus::diag-trace)
target_link_libraries(libArT PUBLIC etl::etl)
target_link_libraries(libArT PUBLIC ArduinoJson)

add_library(libArT::libArT ALIAS libArT)
