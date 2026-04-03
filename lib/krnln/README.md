# krnln 命令元数据目录

该目录用于存放“核心支持库”的命令元数据与平台实现文件。

## 目录约定

- `*.ycmd.json`：命令契约与元数据清单（推荐每个支持库一个文件，使用 `commands` 数组集中声明）。
- `*.ysup.json`：IDE 支持文件（库描述、产物路径、命令入口映射）。
- `window-units.json`：窗口组件、属性、事件等 IDE 元数据。
- `*.protocol.json`：编译协议，描述控件类名、样式与事件映射。
- `impl/windows.cpp`：Windows 平台实现。
- `impl/linux.cpp`：Linux 平台实现。
- `impl/macos.mm`：macOS 平台实现（Objective-C++）。
- `vs2022/`：VS2022 工程（静态库）。

## 约定说明

- `commandId` 使用命名空间格式，例如 `krnln.messageBox`。
- `implementations.<platform>.entry` 为相对当前 `.ycmd.json` 文件目录的路径。
- 编译器/主进程会扫描 `lib/<库名>/**/*.ycmd.json` 并校验实现文件是否存在。
- `window-units.json` 由 IDE 读取，用于工具箱、属性面板、事件栏。
- `*.protocol.json` 由编译器读取，用于把组件事件映射到平台消息。

## 当前示例

- 命令：
  - `krnln.messageBox`（信息框）
  - `krnln.textLength`（取文本长度）
  - `krnln.drawPanel.*`（画板成员命令首批：取设备句柄、清除、取点、画点、画直线）
- 组件：
  - `Canvas`（中文名：画板）

## VS2022 编译

在 `lib/krnln/vs2022` 目录执行：

```powershell
"D:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" .\krnln.sln /t:Build /p:Configuration=Release /p:Platform=x64
```

默认产物路径：

- `lib/krnln/build/windows/x64/Release/krnln.lib`
