cmake_minimum_required(VERSION 3.15)

# コーデックライブラリ
project(SRLACodecLibrary C)
set(CODEC_LIB_NAME srlacodec)
add_library(${CODEC_LIB_NAME}
    STATIC
    $<TARGET_OBJECTS:srla_encoder>
    $<TARGET_OBJECTS:srla_decoder>
    $<TARGET_OBJECTS:srla_coder>
    $<TARGET_OBJECTS:srla_internal>
    $<TARGET_OBJECTS:bit_stream>
    $<TARGET_OBJECTS:lpc>
    )

# デコーダライブラリ
project(SRLADecoderLibrary C)
set(DECODER_LIB_NAME srladec)
add_library(${DECODER_LIB_NAME}
    STATIC
    $<TARGET_OBJECTS:srla_decoder>
    $<TARGET_OBJECTS:srla_coder>
    $<TARGET_OBJECTS:srla_internal>
    $<TARGET_OBJECTS:bit_stream>
    )

# 依存するプロジェクト
add_subdirectory(libs)

# テスト
if(NOT without-test)
    enable_testing()
    # C++環境でないとgtestがビルドできないので、CXXプロジェクトを作る
    # -> Cとの挙動で差異が生じるかもしれない...
    project(SRLATest CXX)
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    else()
        set(CMAKE_CXX_FLAGS "-std=gnu++11") # gtestがGNU独自拡張を使用しているため
        set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    endif()
    include(cmake/gtest.cmake)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_subdirectory(test)
endif()
