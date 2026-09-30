# RackDraw 程序编译配置（与 build/build.bat 等价，可选）
#
# 用法（已安装 MinGW-w64 与 Ninja 时）：
#   cmake -G Ninja -S build -B build/ninja
#   cmake --build build/ninja
# 不使用 CMake 时直接运行 build/build.bat（效果相同，且可在中文路径下直接编译）。
#
# 关键编译选项：
#   · _WIN32_WINNT=0x0601      目标系统 Windows 7 SP1
#   · -static                  静态链接运行时，满足「零依赖」（等效 MSVC 的 /MT）
#   · -municode -mwindows      宽字符入口 + 窗口子系统
#   · 子系统最低版本 6.01

cmake_minimum_required(VERSION 3.16)

# 默认优先使用 32 位工具链：32 位产物可在 32 位与 64 位 Windows 上运行。
if(NOT DEFINED CMAKE_CXX_COMPILER)
    if(DEFINED ENV{MINGW32_BIN})
        set(_mingw32 "$ENV{MINGW32_BIN}")
    else()
        set(_mingw32 "C:/msys64/mingw32/bin")
    endif()
    file(TO_CMAKE_PATH "${_mingw32}" _mingw32)
    if(EXISTS "${_mingw32}/i686-w64-mingw32-g++.exe")
        set(CMAKE_CXX_COMPILER "${_mingw32}/i686-w64-mingw32-g++.exe")
        set(CMAKE_RC_COMPILER "${_mingw32}/windres.exe")
        message(STATUS "toolchain: 32-bit (${_mingw32})")
    else()
        message(STATUS "toolchain: default compiler; a 64-bit exe will not run on 32-bit Windows")
    endif()
endif()

project(RackDraw CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "RackDraw 仅支持 Windows")
endif()

set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../dist)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(APP_OUTPUT_NAME RackDraw)
else()
    set(APP_OUTPUT_NAME RackDraw-x64)
endif()

add_compile_definitions(UNICODE _UNICODE _WIN32_WINNT=0x0601 WINVER=0x0601
                        $<$<CONFIG:Release>:NDEBUG>)

string(TIMESTAMP APP_BUILD_DATE "%Y-%m-%d")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/build_date.h
     "/* 由 CMake 生成：构建日期 */\n#define APP_BUILD_DATE \"${APP_BUILD_DATE}\"\n")

set(COMMON_OPTIONS -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8)

set(APP_SOURCES
    ../src/main.cpp
    ../src/app/single_instance.cpp
    ../src/app/dpi_aware.cpp
    ../src/core/catalog.cpp
    ../src/core/geometry.cpp
    ../src/core/markdown.cpp
    ../src/core/store.cpp
    ../src/core/selftest.cpp
    ../src/ui/theme.cpp
    ../src/ui/rack_view.cpp
    ../src/ui/image_export.cpp
    ../src/ui/dialogs.cpp
    ../src/ui/main_window.cpp
    ../src/util/text_convert.cpp
    ../src/util/shell_open.cpp)

add_executable(RackDraw WIN32 ${APP_SOURCES} ../src/resource/app.rc)

target_include_directories(RackDraw PRIVATE ../src ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(RackDraw PRIVATE ${COMMON_OPTIONS} -O2)
target_link_options(RackDraw PRIVATE
    -municode -static -static-libgcc -static-libstdc++ -s
    -Wl,--major-subsystem-version=6 -Wl,--minor-subsystem-version=1)
target_link_libraries(RackDraw PRIVATE
    gdiplus windowscodecs ole32 oleaut32 uuid comctl32 comdlg32 shell32 gdi32 user32)

set_target_properties(RackDraw PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    OUTPUT_NAME ${APP_OUTPUT_NAME})

# 资源编译（windres）：源文件为 UTF-8，需指定代码页
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001")
endif()
