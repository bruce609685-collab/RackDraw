#pragma once

// 产品信息常量（单一来源，编译期固化进 exe，运行时不读外部文件）。
// 同时被 resource/app.rc 引用（VERSIONINFO 使用其中的字符串/数值形式）。
// 版本口径与 HTML 对照版 v0.2.1 一致：界面标识、导出页脚都用这里的版本号。

#define APP_NAME_CN L"RackDraw 机柜布局设计"
#define APP_NAME_EN L"RackDraw"
#define APP_VERSION L"0.2.1"
// 界面上显示的版本号（带 v 前缀，与 HTML 对照版口径一致）
#define APP_VERSION_TAG L"v" APP_VERSION
#define APP_VERSION_ANSI "0.2.1"
#define APP_VERSION_STR "0.2.1.0"
#define APP_VERSION_CSV 0, 2, 1, 0

// 发布页（F-15 检查更新：交给系统默认浏览器打开，程序自身不发起网络请求）
#define APP_RELEASE_URL L"https://github.com/bruce609685-collab/RackDraw"

// 构建日期（编译时由 build 脚本传入；CMake 走生成的 build_date.h）
#if defined(__has_include)
#  if __has_include("build_date.h")
#    include "build_date.h"
#  endif
#endif
#ifndef APP_BUILD_DATE
#define APP_BUILD_DATE "未指定"
#endif
#define APP_BUILD_DATE_W L"" APP_BUILD_DATE

// 主窗口类名（单实例激活与窗口查找共用）
#define APP_WINDOW_CLASS L"RackDrawMainWindow"
