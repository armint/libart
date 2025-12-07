find_package(Git)

execute_process(COMMAND ${GIT_EXECUTABLE} describe --dirty --always
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  OUTPUT_VARIABLE GIT_VERSION)

string(REGEX REPLACE "v([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\1" VERSION_MAJOR ${GIT_VERSION})
string(REGEX REPLACE "v([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\2" VERSION_MINOR ${GIT_VERSION})
string(REGEX REPLACE "v([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\3" VERSION_BUILD ${GIT_VERSION})

if (GIT_VERSION MATCHES "v([0-9]+)\\.([0-9]+)\\.([0-9]+)-(.+)")
string(REGEX REPLACE "v([0-9]+)\\.([0-9]+)\\.([0-9]+)-(.+)" "\\4" VERSION_BUILDREV ${GIT_VERSION})
set(VERSION_HAVE_BUILDREV "true")
else()
set(VERSION_HAVE_BUILDREV "false")
endif()
