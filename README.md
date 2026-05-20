# dump_parsing

一个基于 Qt 的 Windows 桌面工具，用于分析崩溃 `dump` 文件，并在界面中展示崩溃摘要、崩溃函数、源码定位与源码片段（崩溃行高亮），支持导出报告。

## 功能特性

- 支持选择 `dump + 符号文件`（`.pdb` / `.dbg`）分析
- 支持源码根目录映射，显示崩溃函数源码片段
- 崩溃函数和崩溃偏移高亮展示
- 非阻塞分析（后台线程执行，UI 可继续交互）
- 报告导出：
  - `Markdown (.md)`（默认）
  - `PDF (.pdf)`
  - `TXT (.txt)`
- 带应用图标（窗口/任务栏）

## 分析路径说明

### 1) `dump + .pdb`（MSVC）

走 DbgHelp 路径，基于符号信息解析函数和行号，精度更高。

### 2) `dump + .dbg`（MinGW）

走脚本链路：

1. `tools/generate_windbg_dump_report.cmd` 生成 WinDbg 日志
2. `tools/resolve_mingw_windbg_stack.cmd` 解析模块偏移并还原调用栈

## 运行环境

- Windows 10/11
- Qt 6.5+
- CMake 3.19+
- C++17 编译器（MinGW 或 MSVC）
- DbgHelp（Windows 环境）

`dump + .dbg` 场景建议额外具备：

- MinGW 工具链（如 `addr2line`）
- Debugging Tools for Windows（`cdb.exe` / `windbg.exe`）

## 构建

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/mingw_64
cmake --build build --parallel
```

可执行文件输出：

`bin/dump_parsing.exe`

## 使用方式

1. 选择 `Dump 文件`
2. 选择匹配的 `符号文件 (.pdb/.dbg)`
3. （可选）选择 `源码目录`
4. 点击 `开始分析`
5. 在报告区查看崩溃摘要与源码片段
6. 在导出区域选择目录和格式，点击 `导出报告`

## 输出目录约定

- 脚本目录：`<exe同级>/tools`
- 分析日志目录：`<exe同级>/reports`
- 报告导出目录：默认 `<exe同级>/exports`

## 项目结构（重构后）

```text
dump_parsing/
├─ analyzer/                  # 分析器相关
│  ├─ crash_analyzer.cpp/.h
│  ├─ crash_analyzer_router.cpp
│  ├─ i_analyzer.h
│  ├─ pdb_analyzer.cpp/.h
│  ├─ dbg_analyzer.cpp/.h
│  └─ analyzer_backend.h
├─ ui/                        # UI 相关
│  ├─ main_window.cpp/.h
│  ├─ mainwindow.ui
│  └─ busy_spinner.cpp/.h
├─ common/                    # 通用模块
│  ├─ analyzer_common.cpp/.h
│  └─ report_exporter.cpp/.h
├─ tools/                     # WinDbg/MinGW 辅助脚本
├─ resources/
│  └─ app_icon.ico
├─ main.cpp
├─ app_icon.rc
├─ app_resources.qrc
└─ CMakeLists.txt
```

## 说明

当前核心能力聚焦 Windows 崩溃分析场景。
