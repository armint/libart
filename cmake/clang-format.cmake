# Formatting targets for the sources of libArT itself (src/ and tests/), with the .clang-format of the repository:
# - clangformat:       formats the files in place.
# - clangformat-check: changes nothing; fails and lists the differences if a file is not formatted
#                      (--dry-run -Werror), for use in CI.
# Third-party sources are listed in .clang-format-ignore. clang-format reads that file itself (version 18 and later),
# so these targets, git clang-format and editors skip the same files. Fetched dependencies are outside src/ and
# tests/ and are never globbed.
#
# Different major versions of clang-format format the same file differently, so the version is pinned to the
# clang-format of the CI image (firmware-builder: Clang 20). clang-format-20 is preferred over a plain clang-format;
# set ART_CLANG_FORMAT to use another executable. Without clang-format (or with one older than 18), the targets do
# not exist and the configure continues.
#
# Included from the top-level CMakeLists.txt only when libArT is the top-level project, so that a superproject keeps
# its own targets of the same name.
set(_ART_CLANG_FORMAT_PINNED_MAJOR 20)

find_program(ART_CLANG_FORMAT
        NAMES clang-format-${_ART_CLANG_FORMAT_PINNED_MAJOR} clang-format
        DOC "clang-format executable for the clangformat and clangformat-check targets")
if (NOT ART_CLANG_FORMAT)
    message(STATUS "clang-format not found: the clangformat and clangformat-check targets are not available")
    return()
endif ()

execute_process(COMMAND "${ART_CLANG_FORMAT}" --version
        OUTPUT_VARIABLE _art_clang_format_version_output
        RESULT_VARIABLE _art_clang_format_result
        ERROR_QUIET)
string(REGEX MATCH "version ([0-9]+)" _art_clang_format_version_match "${_art_clang_format_version_output}")
set(_art_clang_format_major "${CMAKE_MATCH_1}")
if (NOT _art_clang_format_result EQUAL 0 OR _art_clang_format_major STREQUAL "")
    message(STATUS "${ART_CLANG_FORMAT} reports no version: the clangformat targets are not available")
    return()
endif ()
if (_art_clang_format_major LESS 18)
    message(STATUS "${ART_CLANG_FORMAT} is version ${_art_clang_format_major}, which ignores .clang-format-ignore "
            "(needs 18 or later): the clangformat targets are not available")
    return()
endif ()
if (NOT _art_clang_format_major EQUAL _ART_CLANG_FORMAT_PINNED_MAJOR)
    message(STATUS "${ART_CLANG_FORMAT} is version ${_art_clang_format_major}, CI uses "
            "${_ART_CLANG_FORMAT_PINNED_MAJOR}: the result of the clangformat targets may differ from CI")
endif ()
message(STATUS "clangformat targets: ${ART_CLANG_FORMAT} (version ${_art_clang_format_major})")

# CONFIGURE_DEPENDS: a new or removed source file is picked up at the next build without a manual re-configure.
file(GLOB_RECURSE _art_clang_format_files
        CONFIGURE_DEPENDS
        RELATIVE "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/src/*.c"
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
        "${PROJECT_SOURCE_DIR}/src/*.h"
        "${PROJECT_SOURCE_DIR}/src/*.hpp"
        "${PROJECT_SOURCE_DIR}/tests/*.c"
        "${PROJECT_SOURCE_DIR}/tests/*.cpp"
        "${PROJECT_SOURCE_DIR}/tests/*.h"
        "${PROJECT_SOURCE_DIR}/tests/*.hpp"
)
list(SORT _art_clang_format_files)

add_custom_target(clangformat
        COMMAND "${ART_CLANG_FORMAT}" -i ${_art_clang_format_files}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Formatting the sources in src/ and tests/ with clang-format"
        VERBATIM)

add_custom_target(clangformat-check
        COMMAND "${ART_CLANG_FORMAT}" --dry-run -Werror ${_art_clang_format_files}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking the format of the sources in src/ and tests/ with clang-format"
        VERBATIM)
