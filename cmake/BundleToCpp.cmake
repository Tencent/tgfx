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

file(READ "${INPUT_FILE}" BINARY_DATA HEX)
string(LENGTH "${BINARY_DATA}" HEX_LENGTH)
math(EXPR BYTE_COUNT "${HEX_LENGTH} / 2")

# Format hex string into comma-separated 0xNN values, 16 per line
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
        string(APPEND ARRAY_CONTENT "    ${LINE},\n")
        set(LINE "")
        set(COL 0)
    endif()
endforeach()
if(COL GREATER 0)
    string(APPEND ARRAY_CONTENT "    ${LINE}\n")
endif()

# Write the C++ source file with self-registration
file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE} — do not edit.
#include <cstddef>
#include <cstdint>
#include \"gpu/EmbeddedShaderBundles.h\"

namespace tgfx {
namespace embedded {

static const uint8_t ${VAR_NAME}[] = {
${ARRAY_CONTENT}};

static const size_t ${VAR_NAME}_Size = ${BYTE_COUNT};

static struct ${VAR_NAME}_Registrar {
  ${VAR_NAME}_Registrar() {
    EmbeddedShaderBundles::Register(Backend::${BACKEND_ENUM}, ${VAR_NAME}, ${VAR_NAME}_Size);
  }
} g_${VAR_NAME}_registrar;

}  // namespace embedded
}  // namespace tgfx
")

message(STATUS "Generated ${OUTPUT_FILE} (${BYTE_COUNT} bytes, backend=${BACKEND_ENUM})")
