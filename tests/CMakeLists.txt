cmake_minimum_required(VERSION 3.20)
Include(FetchContent)

FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12
        GIT_SHALLOW TRUE
)
set(DOCTEST_NO_INSTALL ON)
FetchContent_MakeAvailable(doctest)

FetchContent_Declare(ArduinoJson
        GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
        GIT_TAG v6.21.3
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ArduinoJson)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG 20.37.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(etl)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ryu.cmake")

if (NOT DEFINED PROJECT_NAME)
    # We are not in a subproject
    project(libArT-tests C CXX)
endif ()

# Compiler settings. C++23 by default; a build can ask for another standard with -DCMAKE_CXX_STANDARD=17 or 20 (CI
# runs all three, see .gitlab-ci.yml). Below C++23 the binaries below use the own art::expected and etl::span.
if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif ()
set(CXX_DIALECT gnu)

# Unit libArT-tests
include(CTest)

# Find all test files
file(GLOB TEST_SOURCES
        test_main.cpp
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
        TestCompat.cpp
        storage/TestFatFileReadable.cpp
        storage/TestFatFileWritable.cpp
        usb/TestUsbDevice.cpp
        usb/TestDescriptorBuilder.cpp
        usb/TestUsbEnumeration.cpp
        usb/TestCdcAcmFunction.cpp
)

# The other storage tests, the BlockDevice contract suite and the device and file system mocks use C++20 (designated
# initializers, std::ranges). A C++17 build leaves them out; libArT-compat-tests-cpp23-own runs them with the own types.
set(_ART_CPP20_STORAGE_TESTS
        storage/TestBlockDeviceDecorator.cpp
        storage/BlockDeviceContract.cpp
        storage/TestBlockDevice.cpp
        storage/TestFileSystem.cpp
        storage/TestFatFile.cpp
        storage/TestFatDir.cpp
        storage/TestFatDiskIo.cpp
)
# The same holds for the LittleFS tests except TestLittleFsCompat.cpp, and for TestFileStreams.cpp.
set(_ART_CPP20_LITTLEFS_TESTS
        storage/TestLittleFsFileSystem.cpp
        storage/TestLittleFsReadCache.cpp
        storage/TestLittleFsProgramCache.cpp
        storage/TestLittleFsPoison.cpp
        storage/TestLittleFsConsistency.cpp
        storage/TestFileStreams.cpp
)
if (CMAKE_CXX_STANDARD GREATER_EQUAL 20)
    list(APPEND TEST_SOURCES ${_ART_CPP20_STORAGE_TESTS})
endif ()

# The LittleFS binding runs the real littlefs; its target is defined by the top-level CMakeLists.txt.
if (_ART_NEED_LITTLEFS)
    list(APPEND TEST_SOURCES storage/TestLittleFsCompat.cpp)
    if (CMAKE_CXX_STANDARD GREATER_EQUAL 20)
        list(APPEND TEST_SOURCES ${_ART_CPP20_LITTLEFS_TESTS})
    endif ()
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
        ../src/usb/Device.cpp
        ../src/usb/CdcAcmFunction.cpp
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
        usb/FakeDeviceController.h
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
    # The same choice as art-core (see ART_USE_STD_EXPECTED_SPAN in the top-level CMakeLists.txt). A standalone
    # configure of this directory has no such option, and utils/compat/config.h then derives it from the standard.
    if (DEFINED _ART_USE_STD_EXPECTED_SPAN_VALUE)
        target_compile_definitions(${_art_test_target} PRIVATE ART_USE_STD_EXPECTED_SPAN=${_ART_USE_STD_EXPECTED_SPAN_VALUE})
    endif ()
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

# The compat headers (src/utils/compat/) must work at C++17, where not all of libArT compiles yet, so
# TestCompat.cpp is built into two more binaries of its own, besides libArT-tests (C++23, std::expected/std::span),
# together with the tests of the ported modules (see below):
# - libArT-compat-tests-cpp17: C++17, so the own art::expected and etl::span.
# - libArT-compat-tests-cpp23-own: C++23 with ART_USE_STD_EXPECTED_SPAN=0, the setting of an application that
#   builds at C++17 while libArT builds at C++23; it also compares the sizes of the own and the std types.
# Both use strict warnings, as a C++17 consumer may.
# The tests of src/streams/ and the stream sources they need are built into both binaries as well, so that
# src/streams/ and the templates the tests instantiate (for example Readable::readNumber<T>()) are compiled with
# the own types, at C++17 and at C++23.
set(_ART_COMPAT_STREAM_SOURCES
        TestReadable.cpp
        TestWritable.cpp
        TestBufferedWritable.cpp
        TestStringWritable.cpp
        ../src/streams/Readable.cpp
        ../src/streams/Writable.cpp
        ../src/streams/ArrayWritable.cpp
        ../src/streams/BufferedWritable.cpp
        ../src/utils/tfp_printf.cpp
)
# The same holds for src/storage/ and the file streams on top of it: their sources are built into both binaries, so
# that they compile with the own types at C++17, and so are the tests of the FatFs streams, which need only C++17.
# The LittleFS binding is added below, with littlefs.
set(_ART_COMPAT_STORAGE_SOURCES
        ../src/streams/FatFileReadable.cpp
        ../src/streams/FatFileWritable.cpp
        ../src/streams/FileReadable.cpp
        ../src/streams/FileWritable.cpp
        ../src/storage/File.cpp
        ../src/storage/FileSystem.cpp
        ../src/storage/FatFileSystem.cpp
        ../src/storage/FatFile.cpp
        ../src/storage/FatDir.cpp
        ../src/storage/diskio.cpp
        ../src/storage/fatfs/ff.c
        ../src/storage/fatfs/ffsystem.c
        ../src/storage/fatfs/ffunicode.c
        storage/TestFatFileReadable.cpp
        storage/TestFatFileWritable.cpp
)
# The other storage tests (_ART_CPP20_STORAGE_TESTS above) use C++20, so they are built into
# libArT-compat-tests-cpp23-own only: there they run with the own types.
# The same holds for src/modbus/, src/logging/, src/settings/, src/sensors/, src/usb/, src/utils/ and the header-only
# src/freertos/ (on the FreeRTOS host stubs): their tests, the sources they need and the remaining sources of these
# modules are built into both binaries, so that the templates the tests instantiate compile with the own types too.
set(_ART_COMPAT_MODULE_SOURCES
        TestNumberUtils.cpp
        TestSettings.cpp
        TestModbusMaster.cpp
        TestModbusSlave.cpp
        TestIBMCRC16.cpp
        TestTfpPrintf.cpp
        TestTfpPrintfUtils.cpp
        TestAsymmetricKey.cpp
        TestSHA256.cpp
        TestLogging.cpp
        TestRtos.cpp
        TestKTypeThermocoupleSensor.cpp
        TestSTypeThermocoupleSensor.cpp
        TestCompensatedThermocoupleSensor.cpp
        TestRingBuffer.cpp
        TestFloatText.cpp
        usb/TestUsbDevice.cpp
        usb/TestDescriptorBuilder.cpp
        usb/TestUsbEnumeration.cpp
        usb/TestCdcAcmFunction.cpp
        stubs/timestamp.cpp
        stubs/delay.cpp
        ../src/streams/CountingWritableWrapper.cpp
        ../src/streams/DigestReadable.cpp
        ../src/streams/DigestWritable.cpp
        ../src/streams/AsymmetricKeyReadable.cpp
        ../src/streams/AsymmetricKeyWritable.cpp
        ../src/modbus/ModbusMaster.cpp
        ../src/modbus/ModbusSlave.cpp
        ../src/logging/LogManager.cpp
        ../src/settings/SettingBase.cpp
        ../src/settings/SettingsManager.cpp
        ../src/sensors/ThermocoupleSensor.cpp
        ../src/sensors/CompensatedThermocoupleSensor.cpp
        ../src/usb/Device.cpp
        ../src/usb/CdcAcmFunction.cpp
        ../src/utils/cstring.cpp
        ../src/utils/lists.cpp
        ../src/utils/JSONReader.cpp
        ../src/utils/JSONWriter.cpp
        ../src/utils/streamutils.cpp
        ../src/utils/IBMCRC16Digest.cpp
        ../src/utils/IBMCRC16FastDigest.cpp
        ../src/utils/SHA256Digest.cpp
        ../src/utils/FloatText.cpp
        ../src/utils/ryu_float.c
)
# The LittleFS binding (with littlefs) and its tests, if the littlefs target exists (see libArT-tests above).
# TestLittleFsCompat.cpp runs in both binaries, so that LittleFsFileSystem<> is instantiated and run at C++17. The
# other LittleFS tests and TestFileStreams.cpp (_ART_CPP20_LITTLEFS_TESTS above) use C++20, so they are built into
# libArT-compat-tests-cpp23-own only.
set(_ART_COMPAT_LITTLEFS_SOURCES
        ../src/storage/littlefs/LittleFsFileSystem.cpp
        storage/TestLittleFsCompat.cpp
)
# These files have warnings that are older than the compat binaries and that the strict flags below make fatal.
# Keep them as warnings, for these files only: the variable length array in Readable::readUntil(), the unused
# putstr() in tfp_printf.cpp, the unused parameters of mocks in TestModbusSlave.cpp and TestAsymmetricKey.cpp, the
# unused variables in TestRingBuffer.cpp and TestIBMCRC16.cpp (the latter reported by Clang only) and the unused
# putf_etl() in TestSHA256.cpp.
set_source_files_properties(../src/streams/Readable.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error=vla")
set_source_files_properties(../src/utils/tfp_printf.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error=unused-function")
set_source_files_properties(TestModbusSlave.cpp TestAsymmetricKey.cpp PROPERTIES COMPILE_OPTIONS
        "-Wno-error=unused-parameter")
set_source_files_properties(TestRingBuffer.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error=unused-variable")
set_source_files_properties(TestIBMCRC16.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error=unused-but-set-variable")
set_source_files_properties(TestSHA256.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error=unused-function")
foreach(_art_compat_standard 17 23)
    if (_art_compat_standard EQUAL 17)
        set(_art_compat_target libArT-compat-tests-cpp17)
    else ()
        set(_art_compat_target libArT-compat-tests-cpp23-own)
    endif ()
    add_executable(${_art_compat_target} TestCompat.cpp test_main.cpp ${_ART_COMPAT_STREAM_SOURCES} ${_ART_COMPAT_STORAGE_SOURCES}
            ${_ART_COMPAT_MODULE_SOURCES})
    set_target_properties(${_art_compat_target} PROPERTIES
            CXX_STANDARD ${_art_compat_standard}
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF)
    target_compile_options(${_art_compat_target} PRIVATE -O0 -g -Wall -Wextra -Wpedantic -Werror)
    # UNIT_TESTING: FatFile::MockState, as in libArT-tests.
    target_compile_definitions(${_art_compat_target} PRIVATE UNIT_TESTING)
    target_include_directories(${_art_compat_target} PRIVATE ../src ../src/storage ../src/storage/fatfs . stubs/freertos)
    # Ryu (FloatText.cpp, ryu_float.c) as a system include, so that the strict flags do not apply to its headers.
    target_include_directories(${_art_compat_target} SYSTEM PRIVATE ${ryu_SOURCE_DIR})
    target_link_libraries(${_art_compat_target} PRIVATE etl::etl doctest::doctest ArduinoJson)
    if (_ART_NEED_LITTLEFS)
        target_sources(${_art_compat_target} PRIVATE ${_ART_COMPAT_LITTLEFS_SOURCES})
        target_link_libraries(${_art_compat_target} PRIVATE littlefs)
        target_compile_definitions(${_art_compat_target} PRIVATE
                LITTLEFS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/storage/fixtures")
    endif ()
endforeach()
target_compile_definitions(libArT-compat-tests-cpp23-own PRIVATE ART_USE_STD_EXPECTED_SPAN=0)
target_sources(libArT-compat-tests-cpp23-own PRIVATE ${_ART_CPP20_STORAGE_TESTS})
if (_ART_NEED_LITTLEFS)
    target_sources(libArT-compat-tests-cpp23-own PRIVATE ${_ART_CPP20_LITTLEFS_TESTS})
endif ()

# Discover all libArT-tests
# catch_discover_tests(libArT-tests)
# doctest_discover_tests(libArT-tests) # requires include(doctest)
