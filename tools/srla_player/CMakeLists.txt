cmake_minimum_required(VERSION 3.15)

set(PROJECT_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# プロジェクト名
project(SRLAPlayer C)

# アプリケーション名
set(APP_NAME srlaplayer)

# ライブラリのテストはしない
set(without-test 1)

# 実行形式ファイル
add_executable(${APP_NAME} srla_player.c)

# 依存するサブディレクトリを追加
add_subdirectory(${PROJECT_ROOT_PATH} ${CMAKE_CURRENT_BINARY_DIR}/libsrladec)

# 機種依存のソース追加
if (APPLE)
    set(CMAKE_C_FLAGS "-framework Audiotoolbox -framework CoreAudio -framework CoreServices")
    target_sources(${APP_NAME} PRIVATE srla_player_coreaudio.c)
elseif (MSVC)
    target_sources(${APP_NAME} PRIVATE srla_player_wasapi.c)
elseif (UNIX)
    target_sources(${APP_NAME} PRIVATE srla_player_pulseaudio.c)
endif()

# インクルードパス
target_include_directories(${APP_NAME}
    PRIVATE
    ${PROJECT_ROOT_PATH}/include
    )

# リンクするライブラリ
target_link_libraries(${APP_NAME} srladec)
if (UNIX AND NOT APPLE)
    target_link_libraries(${APP_NAME} pulse-simple pulse m)
endif()

# コンパイルオプション
if(MSVC)
    target_compile_options(${APP_NAME} PRIVATE /W4)
else()
    target_compile_options(${APP_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-aliasing=2 -Wconversion -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition)
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()
set_target_properties(${APP_NAME}
    PROPERTIES
    C_STANDARD 90 C_EXTENSIONS OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
