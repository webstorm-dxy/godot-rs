# 07 · 内置语言服务器（rust-analyzer）

编辑器内置了 rust-analyzer 客户端，所以在 Godot 的脚本编辑器里写 Rust 也能有补全、
悬停和跳转，不需要切到外部 IDE。

## 1. 准备工作

需要安装 `rust-analyzer`（rustup 组件即可）：

```sh
rustup component add rust-analyzer
```

模块会按这个顺序查找它：

1. 环境变量 `GODOT_RUST_ANALYZER`（测试/特殊环境用）；
2. 编辑器设置 `rust/rust_analyzer_path`；
3. `PATH` 里的 `rust-analyzer`；
4. `~/.cargo/bin/rust-analyzer`。

找不到也不会报错：只是补全/悬停这些功能静默关闭（打开 `--verbose` 会看到一行提示）。

## 2. 开关

| 设置 | 默认 | 说明 |
| --- | --- | --- |
| `rust/lsp/enabled`（项目设置） | `true` | 是否启动语言服务器 |
| `rust/rust_analyzer_path`（编辑器设置） | 空 | 指定 rust-analyzer 可执行文件 |

## 3. 有什么体验

| 功能 | 在编辑器里的表现 |
| --- | --- |
| 自动补全 | 在 `.rs` 里按 **Ctrl+Space**（或输入触发）弹出候选，带图标类别（函数/结构体/字段…） |
| 悬停 | 鼠标悬停显示类型/文档；符号查找（Ctrl+点击）优先跳到定义 |
| 跳转定义 | 打开对应的 `.rs` 文件并定位到行 |
| 实时诊断 | rust-analyzer 的 error/warning 出现在脚本编辑器的错误面板与行内标记中 |

诊断与 cargo 构建诊断共用同一套显示机制，所以在构建前的编辑阶段就能看到类型错误。

## 4. 工作原理

```
编辑器（主线程）                    rust-analyzer（子进程）
  validate(text, path)  ──didChange/ didOpen──▶
  complete_code()       ──textDocument/completion──▶
  lookup_code()         ──definition / hover──▶
                        ◀──publishDiagnostics──  写入 RustDiagnostics
                        ◀──response(id)──        响应表（互斥锁）
```

- 通过 `OS.execute_with_pipe` 启动，用 `Content-Length` 帧做 JSON-RPC；
- 读取由后台线程负责，主线程只等待“某个 id 的响应”，并带超时；
- 补全用较短超时（约 300 ms），超时就返回空候选，不阻塞编辑器；
- 文件同步使用全量 `didChange`（`validate()` 会推送当前缓冲区）；
- `initializationOptions` 里设置了 `linkedProjects` 指向项目的 `Cargo.toml`，并关闭
  `cachePriming`，避免在 Godot 项目根目录上做错误的自动发现。

## 5. 调试与自动化

语言服务器作为引擎单例 `RustLsp` 暴露（仅编辑器构建），可以在编辑器脚本里直接调用，
便于排查：

```gdscript
var lsp = Engine.get_singleton("RustLsp")
print(lsp.start(ProjectSettings.globalize_path("res://")))
lsp.sync_document("res://src/player.rs", "fn main() {}\n")
print(lsp.get_diagnostics_for_path("res://src/player.rs"))
print(lsp.complete_at("res://src/player.rs", 0, 3))
print(lsp.hover_at("res://src/player.rs", 0, 3))
print(lsp.definition_at("res://src/player.rs", 0, 3))
```

这也让集成测试可以完全不依赖真实的 rust-analyzer：用一个说 LSP 的假服务端 +
`GODOT_RUST_ANALYZER` 环境变量即可覆盖整条链路。

## 6. 当前限制

- 没有实现格式化（rustfmt）与重命名/代码动作；
- 补全请求使用“光标前的文本”，随后的 `validate()` 会把完整内容推回服务端；
- 编辑器标签页关闭时不会发送 `didClose`（进程关闭时一并结束）。
