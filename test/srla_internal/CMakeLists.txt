cmake_minimum_required(VERSION 3.15)

set(PROJECT_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# テスト名
set(TEST_NAME srla_internal_test)

# 実行形式ファイル
add_executable(${TEST_NAME} main.cpp)

# インクルードディレクトリ
include_directories(${PROJECT_ROOT_PATH}/libs/srla_internal/include)

# リンクするライブラリ
target_link_libraries(${TEST_NAME} gtest gtest_main bit_stream static_huffman)
if (NOT MSVC)
target_link_libraries(${TEST_NAME} pthread)
endif()

# コンパイルオプション
set_target_properties(${TEST_NAME}
    PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

# 実行パスをtmp以下に
add_test(
    NAME srla_internal
    WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_NAME}>/tmp
    COMMAND $<TARGET_FILE:${TEST_NAME}>
    )

# run with: ctest -L lib
set_property(
    TEST srla_internal
    PROPERTY LABELS lib srla_internal
    )

# ビルド後にテストリソースを持ってくる
add_custom_command(
    TARGET ${TEST_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TEST_NAME}>/tmp
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/PriChanIcon.png $<TARGET_FILE_DIR:${TEST_NAME}>/tmp
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/a.wav $<TARGET_FILE_DIR:${TEST_NAME}>/tmp
    )
