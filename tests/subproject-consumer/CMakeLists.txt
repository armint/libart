# A superproject that adds libArT with add_subdirectory(), built by tests/check-subproject.sh. With
# ART_CONSUMER_HAL=ON it also adds span-compat itself, as system-stm32 does, and builds a library (hal.cpp) at C++17
# that links span-compat only. main.cpp passes an art::span to it, which compiles only if both are the same type.
# With ART_CONSUMER_SPAN_COMPAT_PACKAGE=ON, span-compat comes from find_package() (CMAKE_PREFIX_PATH), not from
# FetchContent.
cmake_minimum_required(VERSION 3.20)
project(art-subproject-consumer LANGUAGES C CXX ASM)

option(ART_CONSUMER_HAL "Add span-compat and the library that uses only span-compat" OFF)
option(ART_CONSUMER_SPAN_COMPAT_PACKAGE "Find an installed span-compat package instead of FetchContent" OFF)
set(LIBART_SOURCE_DIR "" CACHE PATH "The libArT checkout")

include(FetchContent)
if (ART_CONSUMER_HAL AND ART_CONSUMER_SPAN_COMPAT_PACKAGE)
    find_package(span-compat 1 REQUIRED CONFIG)
elseif (ART_CONSUMER_HAL)
    # Before libArT, as a superproject that also builds system-stm32 does. The same pin as libArT.
    include("${LIBART_SOURCE_DIR}/cmake/span-compat.cmake")
    FetchContent_Declare(
            span_compat
            GIT_REPOSITORY ${ART_SPAN_COMPAT_GIT_REPOSITORY}
            GIT_TAG ${ART_SPAN_COMPAT_GIT_TAG}
    )
    FetchContent_MakeAvailable(span_compat)
endif ()

add_subdirectory("${LIBART_SOURCE_DIR}" libart)

add_executable(art-subproject-consumer main.cpp)
target_link_libraries(art-subproject-consumer PRIVATE art::core)
if (ART_CONSUMER_HAL)
    add_library(art-consumer-hal STATIC hal.cpp)
    target_link_libraries(art-consumer-hal PUBLIC span-compat::span-compat)
    # As system-stm32. span-compat with SPAN_COMPAT_USE_STD_SPAN=ON raises it to C++20.
    set_target_properties(art-consumer-hal PROPERTIES CXX_STANDARD 17)
    target_link_libraries(art-subproject-consumer PRIVATE art-consumer-hal)
    target_compile_definitions(art-subproject-consumer PRIVATE ART_CONSUMER_HAL=1)
elseif (ART_USE_STD_EXPECTED_SPAN AND TARGET span-compat::span-compat)
    message(FATAL_ERROR "libArT with ART_USE_STD_EXPECTED_SPAN=ON added span-compat")
endif ()
