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

## 画板文本（含 Unicode）注意事项

- `画板.写出 / 写文本行 / 定位写出` 的参数类型都是“文本型”，会走 `wchar_t*` 文本接口，而不是二进制缓冲区接口。
- 编译协议已统一通过 `krnln_text_str(...)` 做安全文本化，再传给 `wchar_t*` 接口，避免直接指针强转带来的风险。
- Windows 实现使用 `TextOutW` 进行实际绘制，可直接显示 Unicode 文本（前提是字体包含对应字形）。
- 对包含 emoji 的文本，Windows 侧会在当前字体缺字形时尝试回退到 `Segoe UI Emoji` 再绘制。
- 切换机制：绘制前先检测当前字体是否缺字形；若缺字形则“临时选中” `Segoe UI Emoji` 绘制，绘制后立即恢复原字体并释放临时字体对象（不改变控件本身字体设置）。
- Linux / macOS 当前仍是占位实现：相关 `drawPanel` 文本函数仅返回 `0`，并未执行实际绘制，因此会表现为“不支持/无效果”。
- 若你的数据是 UTF8/UTF16 二进制，请先通过 `krnln.text.utf8tostr` 或 `krnln.text.utf16tostr` 转回“文本型”再调用画板写出命令。

## 数组操作命令实现位置说明

- `数组操作` 属于核心命令范畴，运行时实现应放在 `impl/commands/windows_core_commands.cpp`（以及对应 Linux/macOS core 命令实现文件）中。
- `impl/components/windows_Canvas.cpp` 仅用于“画板”组件行为实现，不承载核心数组命令逻辑。

### `画板1.定位写出` 传参示例

- 结论：在 Windows 画板上，通常可以直接这样写：`画板1.定位写出(x, y, "中文ABC🙂")`。
- 可以直接传“文本型 Unicode 文本”，例如：`画板1.定位写出(100, 40, "中文ABC🙂")`。
- 如果你手里是 UTF8/UTF16 二进制（字节集），要先转换再传：
  - `画板1.定位写出(100, 40, UTF8到文本(某UTF8字节集))`
  - `画板1.定位写出(100, 40, UTF16到文本(某UTF16字节集))`
- 若 emoji 仍显示为方块，请检查系统/运行环境字体可用性与字符覆盖（例如 `Segoe UI Emoji` 是否可用）。

## VS2022 编译

在 `lib/krnln/vs2022` 目录执行：

```powershell
"D:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" .\krnln.sln /t:Build /p:Configuration=Release /p:Platform=x64
```

默认产物路径：

- `lib/krnln/build/windows/x64/Release/krnln.lib`
