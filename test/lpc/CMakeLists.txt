cmake_minimum_required(VERSION 3.15)

set(PROJECT_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# テスト名
set(TEST_NAME lpc_test)

# 実行形式ファイル
add_executable(${TEST_NAME} main.cpp)

# インクルードディレクトリ
include_directories(${PROJECT_ROOT_PATH}/libs/lpc/include)

# リンクするライブラリ
target_link_libraries(${TEST_NAME} gtest gtest_main fft)
if (NOT MSVC)
target_link_libraries(${TEST_NAME} pthread)
endif()

# コンパイルオプション
set_target_properties(${TEST_NAME}
    PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

add_test(
    NAME lpc
    COMMAND $<TARGET_FILE:${TEST_NAME}>
    )

# run with: ctest -L lib
set_property(
    TEST lpc
    PROPERTY LABELS lib lpc
    )
