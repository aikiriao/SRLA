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
    $<TARGET_OBJECTS:static_huffman>
    $<TARGET_OBJECTS:lpc>
    $<TARGET_OBJECTS:fft>
    )

# デコーダライブラリ
project(SRLADecoderLibrary C)
set(DECODER_LIB_NAME srladec)
add_library(${DECODER_LIB_NAME}
    STATIC
    $<TARGET_OBJECTS:srla_decoder>
    $<TARGET_OBJECTS:srla_coder>
    $<TARGET_OBJECTS:srla_internal>
    $<TARGET_OBJECTS:static_huffman>
    $<TARGET_OBJECTS:bit_stream>
    )

# SIMD命令をどこまで使うか？
set(USE_SIMD_INTRINSICS "" CACHE STRING "Using SIMD operations (SSE41 or AVX2)")
if("${USE_SIMD_INTRINSICS}" STREQUAL "SSE41")
    add_compile_definitions(SRLA_USE_SSE41)
    if(NOT MSVC)
        add_compile_options(-msse4.1)
    endif()
elseif("${USE_SIMD_INTRINSICS}" STREQUAL "AVX2")
    add_compile_definitions(SRLA_USE_AVX2)
    if(MSVC)
        add_compile_options(/arch:AVX2)
    else()
        add_compile_options(-msse4.1 -mavx2)
    endif()
endif()

# 最適化オプション
if(MSVC)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Oi /Ot /Oy /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

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
