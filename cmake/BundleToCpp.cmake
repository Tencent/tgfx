# BundleToCpp.cmake
# Converts a binary file into a C++ source file containing a const array and self-registration.
# Usage: cmake -P BundleToCpp.cmake <input_bin> <output_cpp> <variable_name> <backend_enum>
# backend_enum: Metal, OpenGL, Vulkan, WebGPU

cmake_minimum_required(VERSION 3.12)

set(INPUT_FILE "${CMAKE_ARGV3}")
set(OUTPUT_FILE "${CMAKE_ARGV4}")
set(VAR_NAME "${CMAKE_ARGV5}")
set(BACKEND_ENUM "${CMAKE_ARGV6}")

if(NOT INPUT_FILE OR NOT OUTPUT_FILE OR NOT VAR_NAME OR NOT BACKEND_ENUM)
    message(FATAL_ERROR "Usage: cmake -P BundleToCpp.cmake <input_bin> <output_cpp> <var_name> <backend_enum>")
endif()

file(SIZE "${INPUT_FILE}" BYTE_COUNT)

# Try xxd first (available on macOS/Linux, much faster than CMake hex loop)
find_program(XXD_EXECUTABLE xxd)
if(XXD_EXECUTABLE)
    # Use xxd to generate the array body
    execute_process(
        COMMAND ${XXD_EXECUTABLE} -i "${INPUT_FILE}"
        OUTPUT_VARIABLE XXD_OUTPUT
        RESULT_VARIABLE XXD_RESULT
    )
    if(XXD_RESULT EQUAL 0)
        # xxd outputs: "unsigned char filename[] = { 0x.., ... }; unsigned int filename_len = N;"
        # Extract just the array content between { and };
        string(REGEX REPLACE ".*\\{([^}]*)\\}.*" "\\1" ARRAY_BODY "${XXD_OUTPUT}")
        file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE} — do not edit.
#include <cstddef>
#include <cstdint>
#include \"gpu/EmbeddedShaderBundles.h\"

namespace tgfx {
namespace embedded {

static const uint8_t ${VAR_NAME}[] = {${ARRAY_BODY}};

static const size_t ${VAR_NAME}_Size = ${BYTE_COUNT};

static struct ${VAR_NAME}_Registrar {
  ${VAR_NAME}_Registrar() {
    EmbeddedShaderBundles::Register(Backend::${BACKEND_ENUM}, ${VAR_NAME}, ${VAR_NAME}_Size);
  }
} g_${VAR_NAME}_registrar;

}  // namespace embedded
}  // namespace tgfx
")
        message(STATUS "Generated ${OUTPUT_FILE} (${BYTE_COUNT} bytes, backend=${BACKEND_ENUM}) [xxd]")
        return()
    endif()
endif()

# Fallback: pure CMake hex conversion (slow for large files but always works)
file(READ "${INPUT_FILE}" BINARY_DATA HEX)
string(LENGTH "${BINARY_DATA}" HEX_LENGTH)

set(ARRAY_CONTENT "")
set(LINE "")
set(COL 0)
math(EXPR LAST_INDEX "${HEX_LENGTH} - 1")
foreach(INDEX RANGE 0 ${LAST_INDEX} 2)
    string(SUBSTRING "${BINARY_DATA}" ${INDEX} 2 BYTE)
    if(COL GREATER 0)
        string(APPEND LINE ",")
    endif()
    string(APPEND LINE "0x${BYTE}")
    math(EXPR COL "${COL} + 1")
    if(COL EQUAL 16)
        string(APPEND ARRAY_CONTENT "\n  ${LINE},")
        set(LINE "")
        set(COL 0)
    endif()
endforeach()
if(COL GREATER 0)
    string(APPEND ARRAY_CONTENT "\n  ${LINE}")
endif()

file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE} — do not edit.
#include <cstddef>
#include <cstdint>
#include \"gpu/EmbeddedShaderBundles.h\"

namespace tgfx {
namespace embedded {

static const uint8_t ${VAR_NAME}[] = {${ARRAY_CONTENT}
};

static const size_t ${VAR_NAME}_Size = ${BYTE_COUNT};

static struct ${VAR_NAME}_Registrar {
  ${VAR_NAME}_Registrar() {
    EmbeddedShaderBundles::Register(Backend::${BACKEND_ENUM}, ${VAR_NAME}, ${VAR_NAME}_Size);
  }
} g_${VAR_NAME}_registrar;

}  // namespace embedded
}  // namespace tgfx
")

message(STATUS "Generated ${OUTPUT_FILE} (${BYTE_COUNT} bytes, backend=${BACKEND_ENUM}) [cmake fallback]")
