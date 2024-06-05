cmake_minimum_required(VERSION 3.15)

set(PROJECT_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# テスト名
set(TEST_NAME srla_decoder_test)

# 実行形式ファイル
add_executable(${TEST_NAME}
    srla_decoder_test.cpp
    srla_lpc_synthesize_test.cpp
    main.cpp
    )

# インクルードディレクトリ
include_directories(${PROJECT_ROOT_PATH}/libs/srla_decoder/include)

# リンクするライブラリ
target_link_libraries(${TEST_NAME} gtest gtest_main byte_array bit_stream srla_encoder srla_coder srla_internal lpc fft static_huffman)
if (NOT MSVC)
target_link_libraries(${TEST_NAME} pthread)
endif()

# コンパイルオプション
set_target_properties(${TEST_NAME}
    PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

add_test(
    NAME srla_decoder
    COMMAND $<TARGET_FILE:${TEST_NAME}>
    )

# run with: ctest -L lib
set_property(
    TEST srla_decoder
    PROPERTY LABELS lib srla_decoder
    )
