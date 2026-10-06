# A superproject that adds libArT with add_subdirectory(), built by tests/check-subproject.sh. With
# ART_CONSUMER_HAL=ON it also adds span-compat itself, as system-stm32 does, and builds a library (hal.cpp) that
# links span-compat only. main.cpp passes an art::span to it, which compiles only if both are the same type.
cmake_minimum_required(VERSION 3.20)
project(art-subproject-consumer LANGUAGES C CXX ASM)

option(ART_CONSUMER_HAL "Add span-compat and the library that uses only span-compat" OFF)
set(LIBART_SOURCE_DIR "" CACHE PATH "The libArT checkout")

include(FetchContent)
if (ART_CONSUMER_HAL)
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
    target_link_libraries(art-subproject-consumer PRIVATE art-consumer-hal)
    target_compile_definitions(art-subproject-consumer PRIVATE ART_CONSUMER_HAL=1)
elseif (ART_USE_STD_EXPECTED_SPAN AND TARGET span-compat)
    message(FATAL_ERROR "libArT with ART_USE_STD_EXPECTED_SPAN=ON added span-compat")
endif ()
