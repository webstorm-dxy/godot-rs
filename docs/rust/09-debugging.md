# 09 · 断点调试（lldb-dap）

Rust 代码在游戏进程里是原生代码，所以调试走 **LLDB**：模块内置一个 DAP 客户端，
在编辑器里把游戏跑在 `lldb-dap` 下面，断点、单步、调用栈、变量都在编辑器里看。

## 1. 准备

macOS 上装了 Xcode 命令行工具就有 `lldb-dap`：

```sh
xcode-select --install        # 已有 Xcode 的可以跳过
ls /Library/Developer/CommandLineTools/usr/bin/lldb-dap
```

模块会自动在常见路径和 `PATH` 里找它；找不到（或用 CodeLLDB 等其他适配器）时，
在 **编辑器设置 → Rust → lldb_dap_path** 里填绝对路径。

## 2. 打断点

和 GDScript 一样：打开 `.rs` 文件，在行号左侧点一下（或右键 → *Toggle Breakpoint*）。
断点是编辑器自己的机制，跟着项目走；**不用改代码、也不用加任何宏**。

## 3. 开始调试

底部 **Rust** 面板 → **Debug** 页 → 点 **Debug** 按钮：

1. 先做一次 `cargo build`（debug 档，带调试信息）；
2. 启动 `lldb-dap`，把游戏挂在它下面跑（和你按 F5 跑的东西一样）；
3. 把当前所有 `.rs` 断点下发过去；
4. 命中时自动跳到对应文件的那一行。

工具栏按钮：

| 按钮 | 作用 |
| --- | --- |
| Debug / Stop | 开始 / 结束会话（结束会连游戏进程一起收掉） |
| Continue | 继续运行到下一个断点 |
| Step Over / Into / Out | 单步（跳过 / 进入 / 跳出函数） |

左边是调用栈（双击可以跳到那一帧，含 Godot 自己的 C++ 帧），右边是当前帧的
局部变量与值。

## 4. 在脚本里驱动（CI / 自动化）

调试会话也暴露成编辑器单例 `RustDebug`，可以直接用 GDScript 驱动：

```gdscript
@tool
extends Node

func _ready() -> void:
    var dbg = Engine.get_singleton("RustDebug")
    var project_dir := ProjectSettings.globalize_path("res://")
    var args := PackedStringArray(["--headless", "--path", project_dir])
    var breakpoints := {"res://src/player.rs": [11]}
    print(dbg.start_gd(OS.get_executable_path(), args, project_dir, breakpoints))

func _process(_delta: float) -> void:
    var dbg = Engine.get_singleton("RustDebug")
    if dbg.get_state() == "stopped":
        print(dbg.get_stop_location_gd())   # {file, line, function}
        print(dbg.get_stack_gd())           # [{id, name, file, line}]
        print(dbg.get_variables_gd(0))      # [{name, value, type}]
        dbg.continue_()
```

仓库里的 [`tests/rust/debug_e2e.sh`](../../tests/rust/debug_e2e.sh) 就是这么跑的：
它建一个工程、在 `helper` 里打断点，检查会话确实停在第 11 行、栈顶是那个函数、
参数 `input=21` 可见，再继续运行到结束。

## 5. 限制

- 调试的是**整个游戏进程**（Rust 库在游戏进程里），所以调用栈里会夹杂 Godot 的 C++ 帧；
- 需要 debug 档构建才有完整的变量/行号信息（Debug 按钮会自动用 debug 档构建）；
- 目前只支持“启动并调试”（launch）：不支持 attach 到已经跑起来的进程；
- 断点是运行时下发的：会话开始后再改断点，需要重新开始会话（或按 Stop → Debug）。
