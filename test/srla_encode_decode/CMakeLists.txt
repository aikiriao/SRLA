cmake_minimum_required(VERSION 3.15)

set(PROJECT_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# テスト名
set(TEST_NAME srla_encode_decode_test)

# 実行形式ファイル
add_executable(${TEST_NAME}
    main.cpp
    )

# インクルードディレクトリ
include_directories(${PROJECT_ROOT_PATH}/include)

# リンクするライブラリ
target_link_libraries(${TEST_NAME} gtest gtest_main srla_encoder srla_decoder srla_coder srla_internal byte_array static_huffman bit_stream lpc fft)
if (NOT MSVC)
target_link_libraries(${TEST_NAME} pthread)
endif()

# コンパイルオプション
set_target_properties(${TEST_NAME}
    PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

add_test(
    NAME srla_encode_decode
    COMMAND $<TARGET_FILE:${TEST_NAME}>
    )

# run with: ctest -L lib
set_property(
    TEST srla_encode_decode
    PROPERTY LABELS lib srla_encode_decode
    )
