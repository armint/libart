# Checks that the invalid inputs of the USB descriptor builders (src/usb/DescriptorBuilder.h) fail to compile, with
# the expected diagnostic. Each *.cpp file in this directory is one case. Its first line is "// EXPECT: <regex>":
# - "compiles": the file must compile (the positive control, so that a broken include path cannot hide the rest);
# - otherwise the compiler must fail, and its output must match the regular expression (the name of the detail::
#   function, or "no matching function"), so that an unrelated error does not count as a pass.
#
# The files are compiled at C++17 with the own art::span (ART_USE_STD_EXPECTED_SPAN=0), the span of span-compat.
#
# Usage: cmake -D CXX=<compiler> -D ETL_INCLUDE_DIR=<etl>/include -D SPAN_COMPAT_INCLUDE_DIR=<span-compat>/include
#        -P tests/usb/compile_fail/check.cmake
cmake_minimum_required(VERSION 3.20)

if (NOT CXX)
    message(FATAL_ERROR "Set CXX to the C++ compiler")
endif ()
if (NOT ETL_INCLUDE_DIR OR NOT EXISTS "${ETL_INCLUDE_DIR}/etl/span.h")
    message(FATAL_ERROR "Set ETL_INCLUDE_DIR to the include directory of ETL (<build>/_deps/etl-src/include)")
endif ()
if (NOT SPAN_COMPAT_INCLUDE_DIR OR NOT EXISTS "${SPAN_COMPAT_INCLUDE_DIR}/span-compat/span.h")
    message(FATAL_ERROR
            "Set SPAN_COMPAT_INCLUDE_DIR to the include directory of span-compat (<build>/_deps/span_compat-src/include)")
endif ()

get_filename_component(_case_dir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(_source_dir "${_case_dir}/../../../src" ABSOLUTE)
file(GLOB _cases "${_case_dir}/*.cpp")
list(SORT _cases)
if (NOT _cases)
    message(FATAL_ERROR "No cases in ${_case_dir}")
endif ()

set(_failures 0)
foreach (_case IN LISTS _cases)
    get_filename_component(_name "${_case}" NAME)
    file(STRINGS "${_case}" _first_line LIMIT_COUNT 1)
    if (NOT _first_line MATCHES "^// EXPECT: (.+)$")
        message(SEND_ERROR "${_name}: the first line must be \"// EXPECT: <regex>\"")
        math(EXPR _failures "${_failures} + 1")
        continue()
    endif ()
    set(_expect "${CMAKE_MATCH_1}")

    execute_process(
            COMMAND "${CXX}" -std=c++17 -fsyntax-only -DART_USE_STD_EXPECTED_SPAN=0 -DSPAN_COMPAT_USE_STD_SPAN=0
            "-I${_source_dir}" -isystem "${ETL_INCLUDE_DIR}" -isystem "${SPAN_COMPAT_INCLUDE_DIR}" "${_case}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _output)

    if (_expect STREQUAL "compiles")
        if (_result EQUAL 0)
            message(STATUS "${_name}: compiles - OK")
        else ()
            message(SEND_ERROR "${_name}: must compile, but failed:\n${_output}")
            math(EXPR _failures "${_failures} + 1")
        endif ()
    elseif (_result EQUAL 0)
        message(SEND_ERROR "${_name}: must not compile, but compiled")
        math(EXPR _failures "${_failures} + 1")
    elseif (NOT _output MATCHES "${_expect}")
        message(SEND_ERROR "${_name}: failed, but without \"${_expect}\" in the output:\n${_output}")
        math(EXPR _failures "${_failures} + 1")
    else ()
        message(STATUS "${_name}: rejected with \"${_expect}\" - OK")
    endif ()
endforeach ()

if (_failures GREATER 0)
    message(FATAL_ERROR "${_failures} compile-failure case(s) failed")
endif ()
