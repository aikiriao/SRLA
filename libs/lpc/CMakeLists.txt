cmake_minimum_required(VERSION 3.15)

set(PROJECT_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# プロジェクト名
project(LPC C)

# ライブラリ名
set(LIB_NAME lpc)

# 静的ライブラリ指定
add_library(${LIB_NAME} STATIC)

# ソースディレクトリ
add_subdirectory(src)

# インクルードパス
target_include_directories(${LIB_NAME}
    PRIVATE
    ${PROJECT_ROOT_PATH}/include
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

# コンパイルオプション
if(MSVC)
    target_compile_options(${LIB_NAME} PRIVATE /W4)
else()
    target_compile_options(${LIB_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=2 -Wconversion -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition)
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()
set_target_properties(${LIB_NAME}
    PROPERTIES
    C_STANDARD 90 C_EXTENSIONS OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
