# ShaderLibraryBundleToCpp.cmake
# Converts the shader library bundle into a generated C++ source holding its bytes.
# Usage: cmake -P ShaderLibraryBundleToCpp.cmake <input_bundle> <output_cpp>

cmake_minimum_required(VERSION 3.12)

set(INPUT_FILE "${CMAKE_ARGV3}")
set(OUTPUT_FILE "${CMAKE_ARGV4}")

if(NOT INPUT_FILE OR NOT OUTPUT_FILE)
    message(FATAL_ERROR "Usage: cmake -P ShaderLibraryBundleToCpp.cmake <input_bundle> <output_cpp>")
endif()

file(SIZE "${INPUT_FILE}" BYTE_COUNT)

set(ARRAY_CONTENT "")
find_program(XXD_EXECUTABLE xxd)
if(XXD_EXECUTABLE)
    execute_process(
        COMMAND ${XXD_EXECUTABLE} -i "${INPUT_FILE}"
        OUTPUT_VARIABLE XXD_OUTPUT
        RESULT_VARIABLE XXD_RESULT
    )
    if(XXD_RESULT EQUAL 0)
        string(REGEX REPLACE ".*\\{([^}]*)\\}.*" "\\1" ARRAY_CONTENT "${XXD_OUTPUT}")
    endif()
endif()

if(ARRAY_CONTENT STREQUAL "")
    file(READ "${INPUT_FILE}" BINARY_DATA HEX)
    string(LENGTH "${BINARY_DATA}" HEX_LENGTH)
    math(EXPR LAST_INDEX "${HEX_LENGTH} - 1")
    set(LINE "")
    set(COL 0)
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
endif()

file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE}; do not edit.
#include <cstddef>
#include <cstdint>

namespace tgfx {

namespace {

const uint8_t kShaderLibraryBundleData[] = {${ARRAY_CONTENT}
};

}  // namespace

const uint8_t* GetEmbeddedShaderLibraryBundle(size_t* size) {
  if (size != nullptr) {
    *size = ${BYTE_COUNT};
  }
  return kShaderLibraryBundleData;
}

}  // namespace tgfx
")
message(STATUS "Generated ${OUTPUT_FILE} (${BYTE_COUNT} bytes)")
