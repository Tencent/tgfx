cmake_minimum_required(VERSION 3.13)
project(TGFXBasicAPI)

set(CMAKE_CXX_STANDARD 17)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif ()

# 设置TGFX相关选项
set(TGFX_BUILD_DRAWERS ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# 设置全局编译定义，禁用主TGFX库的绑定
if (DEFINED EMSCRIPTEN)
    add_definitions(-DTGFX_DISABLE_MAIN_BINDINGS=1)
endif()

# 添加TGFX库
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ ./tgfx)

# 收集源文件
set(TGFX_API_FILES src/simple-tgfx-api.cpp)

if (DEFINED EMSCRIPTEN)
    # WebAssembly构建配置
    add_executable(tgfx-simple-api ${TGFX_API_FILES})
    
    # 编译选项
    list(APPEND API_COMPILE_OPTIONS 
        -fno-rtti 
        -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
        -fPIC
    )
    
    # 链接选项
    list(APPEND API_LINK_OPTIONS 
        --no-entry                                          # 不需要main函数
        -lembind                                           # Emscripten绑定库
        -fno-rtti                                          # 禁用RTTI
        -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0             # Emscripten优化
        -sEXPORT_NAME='TGFXSimpleAPI'                     # 导出名称
        -sWASM=1                                          # 生成WebAssembly
        -sMAX_WEBGL_VERSION=2                             # 支持WebGL 2.0
        -sEXPORTED_RUNTIME_METHODS=['GL','HEAPU8']        # 导出运行时方法
        -sMODULARIZE=1                                    # 模块化
        -sENVIRONMENT=web,worker                          # 支持Web和Worker环境
        -sEXPORT_ES6=1                                    # 导出ES6模块
        -sALLOW_MEMORY_GROWTH=1                           # 允许内存增长
        -sINITIAL_MEMORY=16MB                             # 初始内存
        -sUSE_WEBGL2=1                                    # 使用WebGL2
    )
    
    # 调试/发布配置
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND API_COMPILE_OPTIONS -O0 -g3)
        list(APPEND API_LINK_OPTIONS 
            -O0 
            -g3 
            -sSAFE_HEAP=1 
            -Wno-limited-postlink-optimizations
            -sASSERTIONS=1
        )
    else ()
        list(APPEND API_COMPILE_OPTIONS -Oz)
        list(APPEND API_LINK_OPTIONS -Oz --closure 1)
    endif ()
    
    # 应用编译和链接选项
    target_compile_options(tgfx-simple-api PUBLIC ${API_COMPILE_OPTIONS})
    target_link_options(tgfx-simple-api PUBLIC ${API_LINK_OPTIONS})
    
    # 链接TGFX库
    target_link_libraries(tgfx-simple-api tgfx)
    
    # 设置输出文件名和目录
    set_target_properties(tgfx-simple-api PROPERTIES
        OUTPUT_NAME "tgfx-simple-api"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/wasm"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/wasm"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/wasm"
    )
    
else ()
    # 非WebAssembly构建（用于测试）
    add_library(tgfx-simple-api SHARED ${TGFX_API_FILES})
    target_link_libraries(tgfx-simple-api tgfx)
endif ()

# 包含目录
target_include_directories(tgfx-simple-api PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(tgfx-simple-api PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)