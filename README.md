# RackDraw 机柜布局设计

RackDraw 是一款面向弱电工程的机柜布设小工具。
从设备目录中拖拽设备到 19 英寸标准机柜，实时预览安装效果，支持同一 U 位内多设备混排，可一键导出高清布局图。
无需安装、无需联网、零依赖，单个 exe 文件即可运行。
典型使用场景：机房建设前期的设备上架规划、弱电施工方案设计、机柜布设文档交付。

![预览图](https://raw.githubusercontent.com/bruce609685-collab/RackDraw/refs/heads/main/%E9%A2%84%E8%A7%88%E5%9B%BE.jpg)

## 使用

双击 `RackDraw.exe` 即可运行（免安装、免联网）。

| 常用操作 | 方法 |
|---|---|
| 放置设备 | 按住左侧目录中的设备，拖到机柜卡位松手（指针位于图形中心） |
| 同 U 混排 | 同一 U 位内横向摆放多台，总宽不超过 1 个标准宽度 |
| 选中设备 | 单击机柜内设备（只有拖动超过 4 像素才会挪位，手抖不算） |
| 删除设备 | 拖出机柜松手 / 选中后按 `Delete` / 点设备右上角 `×` |
| 自定义设备 | 目录「自定义 → 新建自定义设备」，可选宽度与高度，可勾选存入「我的设备」 |
| 存档设备 | 「我的设备」条目：单击放置；右键可 重命名 / 复制 / 删除 |
| 保存图片 | 「保存图片」导出 2× 高清 PNG / JPG |
| 导出 / 导入布设 | 导出 Markdown 布设表；导入后可「撤销导入」 |
| 缩放 | 滑块、`−`/`＋`、`Ctrl + 滚轮`；`Esc` 取消拖动 |

## 系统要求

| 项目 | 说明 |
|---|---|
| 系统 | Windows 7 SP1 及以上（含 Windows 10 / 11），32 位与 64 位均可 |
| 安装 | 免安装，单个 exe，双击即用 |
| 依赖 | 无。不需要 .NET、VC++ 运行库等任何第三方组件（静态链接） |
| 网络 | 程序不联网（「检查更新」只是用系统浏览器打开本仓库页面） |
| 存档 | 自定义设备存档在 exe 同级目录 `custom-devices.json`（绿色便携，删掉即清空设备存档） |

## 构建

需要 MSYS2 的 MinGW-w64（GCC 支持 C++17，含 windres）。在 `build` 目录下执行：

```bat
cd build
build.bat
```

产物输出到 `dist\RackDraw.exe`（32 位，主产物）与 `dist\RackDraw-x64.exe`（64 位，本机同时存在 64 位工具链时才产出）。

- 工具链默认位于 `C:\msys64\mingw32`（32 位）与 `C:\msys64\mingw64`（64 位），可用环境变量 `MINGW32_BIN` / `MINGW64_BIN` 覆盖。
- 关键编译选项：`-std=c++17 -O2 -DUNICODE -D_WIN32_WINNT=0x0601 -finput-charset=UTF-8 -fexec-charset=UTF-8 -municode -mwindows -static -s`，子系统版本 6.01（Win7 SP1 起）。
- 构建日期在编译时写入程序（`-DAPP_BUILD_DATE`），"关于"对话框与自检报告里显示的就是它。
- `build/CMakeLists.txt` 是与 `build.bat` 等价的 CMake 配置（可选，需已安装 CMake + Ninja）：`cmake -G Ninja -S build -B build/ninja` 后 `cmake --build build/ninja`。

## 代码结构

```
RackDraw待发布/
├── RackDraw.exe                 预编译程序：32 位，可在 32/64 位 Windows 上运行
├── README.md                    本文件：使用、系统要求、构建、自检、代码结构、许可
├── LICENSE                      Apache-2.0 许可证全文
│
├── build/
│   ├── build.bat                一键编译（MinGW-w64；纯 ASCII + 相对路径）
│   └── CMakeLists.txt           与 build.bat 等价的 CMake 配置（可选）
│
└── src/
    ├── main.cpp                 程序入口：命令行分支 → COM/GDI+ 初始化 → 单实例检测 → 主窗口 → 消息循环
    │
    ├── app/                     【启动层】程序级初始化
    │   ├── app_info.h           软件标识常量（名称 / 版本 / 发布页 / 窗口类名，单一来源）
    │   ├── dpi_aware.h          DPI 感知声明与 DPI 比查询接口
    │   ├── dpi_aware.cpp        SetProcessDPIAware 声明；按窗口取 DPI 比
    │   ├── single_instance.h    单实例检测接口
    │   └── single_instance.cpp  命名互斥体；已有实例时激活并前置那个窗口
    │
    ├── core/                    【核心层】与界面无关，可命令行自检
    │   ├── types.h              数据模型：几何常量、Device / State、坐标换算与吸附口径
    │   ├── catalog.h            设备目录接口：类别主题色、标签、LED 与端口口径、名称匹配
    │   ├── catalog.cpp          8 个一级分组的目录数据与上述口径实现
    │   ├── geometry.cpp         版式与几何：U 数 ↔ 孔位换算、取值钳制、冲突判定、吸附、列宽与 U 位文案
    │   ├── markdown.h           布设文件（导出 / 导入）接口
    │   ├── markdown.cpp         Markdown 布设表、ASCII 对照图、「顶边孔位」列、冲突顺延与导入校验
    │   ├── store.h              自定义设备存档接口
    │   ├── store.cpp            便携存档读写（程序目录可写性探测）、脏数据规整、同名去重、副本命名
    │   ├── selftest.h           自检与示例场景接口
    │   └── selftest.cpp         29 条核心逻辑断言 + MakeDemoState 示例场景 + 命令行导出辅助
    │
    ├── ui/                      【界面层】Win32 + GDI+
    │   ├── theme.h              配色与自绘工具接口（调色板、圆角、渐变、文字）
    │   ├── theme.cpp            调色板取值、TextCrisp（按设备像素写字）、TextMiddleRight 等实现
    │   ├── rack_view.h          机柜与设备绘制、几何换算、命中测试接口
    │   ├── rack_view.cpp        立柱 / U 位条纹 / 设备面板与 LED、端口绘制（界面与导出图共用）
    │   ├── image_export.h       布局图导出接口
    │   ├── image_export.cpp     画布布局计算 + WIC（PNG / JPEG）编码，2× 高清离屏重绘
    │   ├── dialogs.h            原生对话框接口
    │   ├── dialogs.cpp          另存为 / 打开、自定义设备弹窗、导入报告、文本输入
    │   ├── main_window.h        主窗口接口（窗口类名 / 创建 / 消息循环）
    │   └── main_window.cpp      主窗口：工具栏、设备目录树、拖拽会话、缩放、提示条、侧栏滚动条
    │
    ├── util/                    【工具层】
    │   ├── text_convert.h       UTF-8 ↔ UTF-16 转换与文件字节解码接口
    │   ├── text_convert.cpp     转换实现（含 BOM 探测）
    │   ├── shell_open.h         打开链接接口
    │   └── shell_open.cpp       用系统默认浏览器打开 URL
    │
    └── resource/                【资源】
        ├── app.rc               资源脚本：图标、清单引用、版本信息
        ├── app.manifest         comctl32 v6、DPI 感知、支持的系统声明
        └── app.ico              程序图标
```

## 许可证

本项目采用 **Apache-2.0** 许可证，详见 [LICENSE](LICENSE)。

Copyright (C) 2026 bruce609685-collab
