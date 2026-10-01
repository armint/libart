# A minimal consumer of the installed art-core package, built by tests/check-install.sh. It uses only what
# find_package(art-core) provides: the include path, the ETL include path, ART_USE_STD_EXPECTED_SPAN and the minimum
# C++ standard all come from art::core.
cmake_minimum_required(VERSION 3.20)
project(art-install-consumer LANGUAGES CXX)

# 1 if the installed art-core uses std::expected and std::span, 0 for the own types. main.cpp checks that the
# exported ART_USE_STD_EXPECTED_SPAN has this value.
set(ART_CONSUMER_EXPECT_STD_TYPES "" CACHE STRING "Expected value of ART_USE_STD_EXPECTED_SPAN")
if (NOT ART_CONSUMER_EXPECT_STD_TYPES MATCHES "^[01]$")
    message(FATAL_ERROR "Set ART_CONSUMER_EXPECT_STD_TYPES to 0 or 1")
endif ()

find_package(art-core REQUIRED)

# One translation unit per installed header, so that every installed header compiles on its own with only the
# installed package. Left out: freertos/, which needs FreeRTOS from the consumer of art::rtos, and the FatFs headers
# in storage/fatfs/, which upstream does not make self-contained (diskio.h needs ff.h first); storage/FatFileSystem.h
# includes them in the right order.
get_target_property(_art_include_dirs art::core INTERFACE_INCLUDE_DIRECTORIES)
list(GET _art_include_dirs 0 _art_include_dir)
file(GLOB_RECURSE _art_headers RELATIVE "${_art_include_dir}" CONFIGURE_DEPENDS
        "${_art_include_dir}/art_types.h"
        "${_art_include_dir}/logging/*.h"
        "${_art_include_dir}/modbus/*.h"
        "${_art_include_dir}/sensors/*.h"
        "${_art_include_dir}/settings/*.h"
        "${_art_include_dir}/storage/*.h"
        "${_art_include_dir}/streams/*.h"
        "${_art_include_dir}/utils/*.h")
list(FILTER _art_headers EXCLUDE REGEX "^storage/fatfs/")
if (NOT _art_headers)
    message(FATAL_ERROR "No libArT headers found in ${_art_include_dir}")
endif ()
set(_header_sources "")
foreach (_header IN LISTS _art_headers)
    string(MAKE_C_IDENTIFIER "${_header}" _name)
    set(_source "${CMAKE_CURRENT_BINARY_DIR}/headers/${_name}.cpp")
    file(CONFIGURE OUTPUT "${_source}" CONTENT "#include \"${_header}\"\n")
    list(APPEND _header_sources "${_source}")
endforeach ()

add_executable(art-install-consumer main.cpp ${_header_sources})
# ArduinoJson: settings/ and utils/JSONWriter.h include ArduinoJson.h, and art::core does not link it (as with
# add_subdirectory()). find_package(art-core) has found it.
target_link_libraries(art-install-consumer PRIVATE art::core ArduinoJson)
target_compile_definitions(art-install-consumer PRIVATE
        ART_CONSUMER_EXPECT_STD_TYPES=${ART_CONSUMER_EXPECT_STD_TYPES})
