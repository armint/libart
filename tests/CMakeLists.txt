cmake_minimum_required(VERSION 3.20)
Include(FetchContent)

FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12
)
set(DOCTEST_NO_INSTALL ON)
FetchContent_MakeAvailable(doctest)

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

if (NOT DEFINED PROJECT_NAME)
    # We are not in a subproject
    project(libArT-tests C CXX)
endif ()

# Compiler settings
set(CMAKE_CXX_STANDARD 23)
set(CXX_DIALECT gnu)

# Unit libArT-tests
include(CTest)

# Find all test files
file(GLOB TEST_SOURCES
        TestNumberUtils.cpp
        TestReadable.cpp
        TestSettings.cpp
        TestCharacterSkippingReadable.cpp
        TestModbusMaster.cpp
        TestModbusSlave.cpp
        TestIBMCRC16.cpp
        TestWritable.cpp
        TestBufferedWritable.cpp
        TestStringWritable.cpp
        TestTfpPrintf.cpp
        TestAsymmetricKey.cpp
        TestSHA256.cpp
        TestPseudoRandomReadable.cpp
        TestTfpPrintfUtils.cpp
        TestLogging.cpp
        TestRtos.cpp
        TestKTypeThermocoupleSensor.cpp
        TestSTypeThermocoupleSensor.cpp
        TestCompensatedThermocoupleSensor.cpp
        TestRingBuffer.cpp
        storage/TestBlockDeviceDecorator.cpp
        storage/BlockDeviceContract.cpp
        storage/TestBlockDevice.cpp
        storage/TestFileSystem.cpp
        storage/TestFatFile.cpp
        storage/TestFatDir.cpp
        storage/TestFatFileReadable.cpp
        storage/TestFatFileWritable.cpp
)

# The LittleFS binding runs the real littlefs; its target is defined by the top-level CMakeLists.txt.
if (_ART_NEED_LITTLEFS)
    list(APPEND TEST_SOURCES storage/TestLittleFsSmoke.cpp storage/TestFileStreams.cpp)
endif ()

file(GLOB TO_TEST_SOURCES
        ../src/streams/Readable.cpp
        ../src/streams/PseudoRandomReadable.cpp
        ../src/streams/ArrayWritable.cpp
        ../src/streams/CountingWritableWrapper.cpp
        ../src/streams/CharacterSkippingReadable.cpp
        ../src/streams/Writable.cpp
        ../src/streams/BufferedWritable.cpp
        ../src/streams/DigestReadable.cpp
        ../src/streams/DigestWritable.cpp
        ../src/streams/AsymmetricKeyWritable.cpp
        ../src/streams/AsymmetricKeyReadable.cpp
        ../src/streams/FatFileReadable.cpp
        ../src/streams/FatFileWritable.cpp
        ../src/streams/FileReadable.cpp
        ../src/streams/FileWritable.cpp
        ../src/modbus/ModbusMaster.cpp
        ../src/modbus/ModbusSlave.cpp
        ../src/logging/LogManager.cpp
        ../src/settings/SettingBase.cpp
        ../src/settings/SettingsManager.cpp
        ../src/utils/lists.cpp
        ../src/storage/File.cpp
        ../src/storage/FileSystem.cpp
        ../src/storage/FatFileSystem.cpp
        ../src/storage/FatFile.cpp
        ../src/storage/FatDir.cpp
        ../src/storage/diskio.cpp
        ../src/storage/fatfs/ff.c
        ../src/storage/fatfs/ffsystem.c
        ../src/storage/fatfs/ffunicode.c
        ../src/sensors/ThermocoupleSensor.cpp
        ../src/sensors/CompensatedThermocoupleSensor.cpp
)

file(GLOB ADDITIONAL_SOURCES
        stubs/timestamp.cpp
        stubs/delay.cpp
        ../src/utils/tfp_printf.cpp
        ../src/utils/JSONReader.cpp
        ../src/utils/JSONWriter.cpp
        ../src/utils/streamutils.cpp
        ../src/utils/IBMCRC16Digest.cpp
        ../src/utils/IBMCRC16FastDigest.cpp
        ../src/utils/SHA256Digest.cpp
        RamDisk.h
        storage/RamBlockDevice.h
        storage/InMemoryFileSystem.h
        storage/MockCipherBlockDevice.h
        storage/BlockDeviceContract.h
)


add_executable(libArT-tests ${TEST_SOURCES} ${TO_TEST_SOURCES} ${ADDITIONAL_SOURCES})

target_compile_options(libArT-tests PUBLIC -O0 -g)
# Needed for FatFile::MockState/FatFile::state, used by
# storage/TestFatFileReadable.cpp and storage/TestFatFileWritable.cpp.
target_compile_definitions(libArT-tests PRIVATE UNIT_TESTING)
target_include_directories(libArT-tests PRIVATE
        ../src
        ../src/storage
        .
        ../src/storage/fatfs
        stubs/freertos
        include
)

# strlcpy support
#target_compile_options(libArT-tests PUBLIC -include "bsd/string.h")
target_link_libraries(libArT-tests PUBLIC bsd)

if (_ART_NEED_LITTLEFS)
    target_sources(libArT-tests PRIVATE ../src/storage/littlefs/LittleFsFileSystem.cpp)
    target_link_libraries(libArT-tests PRIVATE littlefs)
endif ()

# ETL
target_link_libraries(libArT-tests PRIVATE etl::etl)
# doctest
target_link_libraries(libArT-tests PRIVATE doctest::doctest)
target_include_directories(libArT-tests PRIVATE ${doctest_SOURCE_DIR})
# JSON library
target_link_libraries(libArT-tests PUBLIC ArduinoJson)

# Discover all libArT-tests
# catch_discover_tests(libArT-tests)
# doctest_discover_tests(libArT-tests) # requires include(doctest)
