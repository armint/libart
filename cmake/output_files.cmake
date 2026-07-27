# ADD_DUMP_TARGET(TARGET)
#
# Adds a <TARGET>.dump custom target that runs objdump -x -D -S -s on the
# built binary and writes the result to <TARGET>.dump alongside the binary.
# Useful for inspecting the final linked image.

function(ADD_DUMP_TARGET TARGET)
    if (EXECUTABLE_OUTPUT_PATH)
        set(FILENAME "${EXECUTABLE_OUTPUT_PATH}/${TARGET}")
    else ()
        set(FILENAME "${TARGET}")
    endif ()
    add_custom_target(${TARGET}.dump
            DEPENDS ${TARGET}
            COMMAND ${CMAKE_OBJDUMP} -x -D -S -s ${FILENAME} | ${CMAKE_CPPFILT} > ${FILENAME}.dump
    )
endfunction()

# PRINT_SIZE_OF_TARGETS(TARGET)
#
# Attaches a post-build command to TARGET that prints the section sizes of
# the built binary using arm-none-eabi-size (or the configured CMAKE_SIZE).

function(PRINT_SIZE_OF_TARGETS TARGET)
    if (EXECUTABLE_OUTPUT_PATH)
        set(FILENAME "${EXECUTABLE_OUTPUT_PATH}/${TARGET}")
    else ()
        set(FILENAME "${TARGET}")
    endif ()
    add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_SIZE} ${FILENAME}
    )
endfunction()
