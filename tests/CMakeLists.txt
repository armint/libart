cmake_minimum_required(VERSION 3.20)
Include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.11.0
)
FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(ArduinoJson
        GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
        GIT_TAG v6.21.3
)
FetchContent_MakeAvailable(ArduinoJson)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG 20.37.0
)
FetchContent_MakeAvailable(etl)

FetchContent_Declare(utils-lists
        GIT_REPOSITORY ssh://git@gitlab.vandertogt.org:3022/ht-oven/utils-lists-xpack.git
        GIT_TAG d63335a397d548a48365fb2107bf59a4714f50cd
)

FetchContent_MakeAvailable(utils-lists)

FetchContent_Declare(diag-trace
        GIT_REPOSITORY https://github.com/micro-os-plus/diag-trace-xpack.git
        GIT_TAG v4.2.0
)

FetchContent_MakeAvailable(diag-trace)

if (NOT DEFINED PROJECT_NAME)
    # We are not in a subproject
    project(libArT-tests C CXX)
endif ()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# Unit libArT-tests
include(CTest)
include(Catch)

# Compiler settings
set(CMAKE_CXX_STANDARD 23)
set(CXX_DIALECT gnu)

# Find all test files
file(GLOB TEST_SOURCES
        TestReadable.cpp
        TestSettings.cpp
        TestCharacterSkippingReadable.cpp
        TestModbusMaster.cpp
        TestModbusSlave.cpp
        TestIBMCRC16.cpp
        TestWritable.cpp
        TestBufferedWritable.cpp
)

file(GLOB TO_TEST_SOURCES
        ../src/streams/Readable.cpp
        ../src/streams/ArrayReadable.cpp
        ../src/streams/ArrayWritable.cpp
        ../src/streams/CountingWritableWrapper.cpp
        ../src/streams/CharacterSkippingReadable.cpp
        ../src/streams/StringReadable.cpp
        ../src/streams/Writable.cpp
        ../src/streams/DigestReadable.cpp
        ../src/streams/DigestWritable.cpp
        ../src/modbus/ModbusMaster.cpp
        ../src/modbus/ModbusSlave.cpp
        ../src/settings/SettingBase.cpp
        ../src/settings/SettingsManager.cpp
)

file(GLOB ADDITIONAL_SOURCES
        stubs/systick.cpp
        stubs/delay.cpp
        ../src/utils/tfp_printf.cpp
        ../src/utils/JSONReader.cpp
        ../src/utils/JSONWriter.cpp
        ../src/utils/streamutils.cpp
        ../src/utils/IBMCRC16Digest.cpp
        ../src/utils/IBMCRC16FastDigest.cpp
)


add_executable(libArT-tests ${TEST_SOURCES} ${TO_TEST_SOURCES} ${ADDITIONAL_SOURCES})

target_compile_options(libArT-tests PUBLIC -O0 -g)
target_include_directories(libArT-tests PRIVATE
        ../src
        include
)

# strlcpy support
target_compile_options(libArT-tests PUBLIC -include "bsd/string.h")
target_link_libraries(libArT-tests PUBLIC bsd)
target_link_libraries(libArT-tests PUBLIC micro-os-plus::utils-lists)
target_link_libraries(libArT-tests PUBLIC micro-os-plus::diag-trace)

# ETL
target_link_libraries(libArT-tests PRIVATE etl::etl)
# Catch2
target_link_libraries(libArT-tests PUBLIC Catch2::Catch2WithMain)
# JSON library
target_link_libraries(libArT-tests PUBLIC ArduinoJson)

# Discover all libArT-tests
catch_discover_tests(libArT-tests)
