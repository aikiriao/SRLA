cmake_minimum_required(VERSION 3.15)

# プロジェクト名
project(ByteArray C)

# ライブラリ名
set(LIB_NAME byte_array)

# ライブラリ
add_library(${LIB_NAME} INTERFACE)

# インクルードパス
target_include_directories(${LIB_NAME}
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
