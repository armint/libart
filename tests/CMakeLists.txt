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

# See the top-level CMakeLists.txt for why this is a pinned tarball, populate-only.
FetchContent_Declare(ryu
        URL https://github.com/ulfjack/ryu/archive/4c0618b0e44f7ef027ebae05d2cc7812048f7c8f.tar.gz
        URL_HASH SHA256=5c039f2957769c5f61a8699b12d8703043b3f47c3ac6e2124c5a04afb6819f29
)
FetchContent_GetProperties(ryu)
if(NOT ryu_POPULATED)
    FetchContent_Populate(ryu)
endif()

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
        TestFloatText.cpp
        storage/TestBlockDeviceDecorator.cpp
        storage/BlockDeviceContract.cpp
        storage/TestBlockDevice.cpp
        storage/TestFileSystem.cpp
        storage/TestFatFile.cpp
        storage/TestFatDir.cpp
        storage/TestFatDiskIo.cpp
        storage/TestFatFileReadable.cpp
        storage/TestFatFileWritable.cpp
)

# The LittleFS binding runs the real littlefs; its target is defined by the top-level CMakeLists.txt.
if (_ART_NEED_LITTLEFS)
    list(APPEND TEST_SOURCES storage/TestLittleFsFileSystem.cpp storage/TestLittleFsReadCache.cpp storage/TestLittleFsProgramCache.cpp storage/TestLittleFsCompat.cpp storage/TestLittleFsPoison.cpp storage/TestLittleFsConsistency.cpp storage/TestFileStreams.cpp)
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
        ../src/utils/FloatText.cpp
        ../src/utils/ryu_float.c
        storage/FatTestDisk.h
        storage/RamBlockDevice.h
        storage/PowerCutDevice.h
        storage/InMemoryFileSystem.h
        storage/MockCipherBlockDevice.h
        storage/BlockDeviceContract.h
)


add_executable(libArT-tests ${TEST_SOURCES} ${TO_TEST_SOURCES} ${ADDITIONAL_SOURCES})
add_executable(libArT-tests-64bit ${TEST_SOURCES} ${TO_TEST_SOURCES} ${ADDITIONAL_SOURCES})
add_executable(libArT-tests-float ${TEST_SOURCES} ${TO_TEST_SOURCES} ${ADDITIONAL_SOURCES})
add_executable(libArT-tests-float-64bit ${TEST_SOURCES} ${TO_TEST_SOURCES} ${ADDITIONAL_SOURCES})

# LIBART_ENABLE_64BIT switches big_int_t/big_uint_t between int32_t/uint32_t
# and int64_t/uint64_t (src/art_types.h) and gates the ll-sized paths in
# tfp_printf.cpp. art-core ships without it by default, so libArT-tests
# stays in that (32-bit) configuration to match what actually ships;
# libArT-tests-64bit builds the same sources with the macro defined, so
# both configurations run in CI instead of one silently going untested.
#
# PRINTF_ENABLE_FLOATING_POINT turns on tfp_printf's %f/%F path. art-core
# ships without it by default too (it isn't defined anywhere outside these
# tests), so it is deliberately left off libArT-tests and libArT-tests-64bit
# to keep CI covering that default (float-disabled) configuration on both
# integer widths. libArT-tests-float and libArT-tests-float-64bit turn the
# macro on for each width, so TestTfpPrintf.cpp's %f cases (including the
# width-dependent overflow boundary at 2^32 vs 2^64) actually run instead of
# staying dead code.
foreach(_art_test_target libArT-tests libArT-tests-64bit libArT-tests-float libArT-tests-float-64bit)
    target_compile_options(${_art_test_target} PUBLIC -O0 -g)
    # Needed for FatFile::MockState/FatFile::state, used by
    # storage/TestFatFileReadable.cpp and storage/TestFatFileWritable.cpp.
    target_compile_definitions(${_art_test_target} PRIVATE UNIT_TESTING)
    target_include_directories(${_art_test_target} PRIVATE
            ../src
            ../src/storage
            .
            ../src/storage/fatfs
            stubs/freertos
            include
    )

    # strlcpy support
    #target_compile_options(${_art_test_target} PUBLIC -include "bsd/string.h")
    target_link_libraries(${_art_test_target} PUBLIC bsd)

    if (_ART_NEED_LITTLEFS)
        target_sources(${_art_test_target} PRIVATE ../src/storage/littlefs/LittleFsFileSystem.cpp)
        target_link_libraries(${_art_test_target} PRIVATE littlefs)
        # Volume images written by earlier littlefs versions, read by storage/TestLittleFsCompat.cpp.
        target_compile_definitions(${_art_test_target} PRIVATE LITTLEFS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/storage/fixtures")
    endif ()

    # Ryu (FloatText.h/.cpp); private, see the top-level CMakeLists.txt.
    target_include_directories(${_art_test_target} PRIVATE ${ryu_SOURCE_DIR})
    # ETL
    target_link_libraries(${_art_test_target} PRIVATE etl::etl)
    # doctest
    target_link_libraries(${_art_test_target} PRIVATE doctest::doctest)
    target_include_directories(${_art_test_target} PRIVATE ${doctest_SOURCE_DIR})
    # JSON library
    target_link_libraries(${_art_test_target} PUBLIC ArduinoJson)
endforeach()

target_compile_definitions(libArT-tests-64bit PRIVATE LIBART_ENABLE_64BIT)
target_compile_definitions(libArT-tests-float PRIVATE PRINTF_ENABLE_FLOATING_POINT)
target_compile_definitions(libArT-tests-float-64bit PRIVATE PRINTF_ENABLE_FLOATING_POINT LIBART_ENABLE_64BIT)

# TestFloatText.cpp's exhaustive round-trip case (doctest::skip()'d by default, see the file)
# walks all 2^32 float bit patterns; every other target here builds at -O0 for debuggability,
# which would make that loop impractically slow. Override just the files on its hot path back up
# to -O2 (last -O flag on the command line wins over the target's -O0), so the dedicated CI job
# that runs it with --no-skip=true finishes in a reasonable time. This affects every one of the
# four binaries equally (harmless: FloatText does not depend on LIBART_ENABLE_64BIT or
# PRINTF_ENABLE_FLOATING_POINT), but the exhaustive case itself only actually runs in that job.
set_source_files_properties(
        TestFloatText.cpp
        ../src/utils/FloatText.cpp
        ../src/utils/ryu_float.c
        PROPERTIES COMPILE_OPTIONS "-O2"
)

# Discover all libArT-tests
# catch_discover_tests(libArT-tests)
# doctest_discover_tests(libArT-tests) # requires include(doctest)
