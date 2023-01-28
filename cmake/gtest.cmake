cmake_minimum_required(VERSION 3.15)

# Google Test settings
include(ExternalProject)

# gtest追加
ExternalProject_Add(GoogleTest
    GIT_REPOSITORY  https://github.com/google/googletest.git
    GIT_TAG         release-1.12.0
    INSTALL_COMMAND   ""
    LOG_DOWNLOAD ON
    )

# インクルードパス追加
ExternalProject_Get_Property(GoogleTest source_dir)
include_directories(${source_dir}/googletest/include)

# 成果物のディレクトリ取得
ExternalProject_Get_Property(GoogleTest binary_dir)

# ライブラリ追加
add_library(gtest STATIC IMPORTED)
if(MSVC)
    set_target_properties(gtest
        PROPERTIES
        IMPORTED_LOCATION "${binary_dir}/lib/Debug/gtest.lib"
        IMPORTED_LOCATION_DEBUG "${binary_dir}/lib/Debug/gtest.lib"
        IMPORTED_LOCATION_RELEASE "${binary_dir}/lib/Release/gtest.lib"
        )
else()
    set_target_properties(gtest
        PROPERTIES
        IMPORTED_LOCATION ${binary_dir}/lib/libgtest.a
        )
endif()

# メインエントリ追加
add_library(gtest_main STATIC IMPORTED)
if(MSVC)
    set_target_properties(gtest_main
        PROPERTIES
        IMPORTED_LOCATION "${binary_dir}/lib/Debug/gtest_main.lib"
        IMPORTED_LOCATION_DEBUG "${binary_dir}/lib/Debug/gtest_main.lib"
        IMPORTED_LOCATION_RELEASE "${binary_dir}/lib/Release/gtest_main.lib"
        )
else()
    set_target_properties(gtest_main
        PROPERTIES
        IMPORTED_LOCATION ${binary_dir}/lib/libgtest_main.a
        )
endif()
