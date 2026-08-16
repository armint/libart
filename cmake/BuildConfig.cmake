# art_setup_build_config(NAME)
#
# Creates an INTERFACE CMake target named NAME and attaches compile
# definitions, compile options, and linker options sourced from variables
# defined by the including project:
#
#   PROJECT_DEFINES      — compile definitions applied to all languages
#   DEBUG_DEFINES        — compile definitions applied in Debug builds only
#   COMMON_OPTIONS       — compile options applied to all languages
#   CXX_OPTIONS          — compile options applied to C++ sources only
#   C_OPTIONS            — compile options applied to C sources only
#   DEBUG_OPTIONS        — compile options applied in Debug builds only
#   LINKER_OPTIONS       — link options propagated to consumers that link
#   PROJECT_INCLUDE_DIRS — include directories propagated to consumers
#   ADDITIONAL_INCLUDE_DIRS — additional include directories for consumers
#
# Call once at the top of the superproject CMakeLists.txt before adding
# any subdirectories that consume the target.

function(art_setup_build_config NAME)
    if (NOT NAME)
        message(FATAL_ERROR "art_setup_build_config(NAME): NAME is required")
    endif ()

    if (TARGET ${NAME})
        return()
    endif ()

    add_library(${NAME} INTERFACE)

    if (DEFINED PROJECT_DEFINES)
        target_compile_definitions(${NAME} INTERFACE ${PROJECT_DEFINES})
    endif ()
    if (DEFINED DEBUG_DEFINES)
        target_compile_definitions(${NAME} INTERFACE "$<$<CONFIG:Debug>:${DEBUG_DEFINES}>")
    endif ()

    if (DEFINED COMMON_OPTIONS)
        target_compile_options(${NAME} INTERFACE ${COMMON_OPTIONS})
    endif ()
    if (DEFINED CXX_OPTIONS)
        target_compile_options(${NAME} INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:${CXX_OPTIONS}>")
    endif ()
    if (DEFINED C_OPTIONS)
        target_compile_options(${NAME} INTERFACE "$<$<COMPILE_LANGUAGE:C>:${C_OPTIONS}>")
    endif ()
    if (DEFINED DEBUG_OPTIONS)
        target_compile_options(${NAME} INTERFACE "$<$<CONFIG:Debug>:${DEBUG_OPTIONS}>")
    endif ()

    if (DEFINED LINKER_OPTIONS)
        target_link_options(${NAME} INTERFACE ${LINKER_OPTIONS})
    endif ()

    if (DEFINED PROJECT_INCLUDE_DIRS OR DEFINED ADDITIONAL_INCLUDE_DIRS)
        target_include_directories(${NAME} INTERFACE
                ${PROJECT_INCLUDE_DIRS}
                ${ADDITIONAL_INCLUDE_DIRS}
        )
    endif ()
endfunction()
