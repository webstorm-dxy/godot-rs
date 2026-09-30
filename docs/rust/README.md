# 在 Godot 里用 Rust 写游戏（Rust 模块文档）

这是一套面向初学者的文档，介绍**这个 Godot 分支内置的 Rust 模块**：它把 Rust 变成
Godot 的一等语言，用法尽量贴近 GDScript。

## 这套文档适合谁

- 会一点 Rust（或刚开始学），想把 Rust 用在 Godot 里；
- 不想在每个项目里手工维护 `.gdextension` 文件；
- 希望“新建项目 → 写代码 → 按 F5”就能跑起来。

## 阅读顺序

**第一次接触？从教程开始 → [10-tutorial.md](10-tutorial.md)**（手把手做一个完整小游戏，每步都有代码和预期输出）

| 文档 | 内容 |
| --- | --- |
| [01-installation.md](01-installation.md) | 编译带 Rust 模块的编辑器、环境要求 |
| [02-project-setup.md](02-project-setup.md) | 新建项目、目录结构、设置项 |
| [03-writing-scripts.md](03-writing-scripts.md) | 写第一个可挂载的 Rust 脚本：属性、方法、信号、生命周期 |
| [04-editor-workflow.md](04-editor-workflow.md) | 编辑器里的完整工作流：附加脚本、构建、运行、错误定位 |
| [05-troubleshooting.md](05-troubleshooting.md) | 常见报错与排查 |
| [06-architecture.md](06-architecture.md) | 模块内部原理、当前支持范围与路线图 |
| [07-language-server.md](07-language-server.md) | 内置 rust-analyzer：补全、悬停、跳转、实时诊断 |
| [08-exporting.md](08-exporting.md) | 导出游戏：动态库怎么进产物、跨平台怎么办 |
| [09-debugging.md](09-debugging.md) | 断点调试：`.rs` 里打断点、单步、调用栈、变量 |
| [10-tutorial.md](10-tutorial.md) | **手把手教程**：从建项目到导出，做一个“接星星”小游戏 |

## 30 秒快速上手（已编译好引擎的前提下）

```sh
# 1. 新建一个带 Rust 工程的项目
./bin/godot.macos.editor.arm64.rust --headless --editor \
    --rust-init-project /path/to/MyGame --quit

# 2. 用编辑器打开它（正常 GUI 操作：项目 → 运行，会自动 cargo build）
./bin/godot.macos.editor.arm64.rust --path /path/to/MyGame -e
```

生成的 `src/player.rs` 就是一个可以直接挂到节点上的脚本，改完按 **F5**：编辑器会先
`cargo build`，再启动游戏。

## 名词对照

| 名词 | 含义 |
| --- | --- |
| 可挂载脚本（script） | 挂在**已有节点**上的逻辑，类似 `.gd` 文件；一个 `.rs` 文件对应一个脚本类 |
| 节点类型（node class） | 自己就是一种节点（出现在“添加节点”对话框），由 `#[derive(GodotClass)]` 定义 |
| 描述符（descriptor） | Rust 侧注册给引擎的“类名 / 基类 / 属性 / 方法 / 信号”信息，编辑器据此显示检视面板和信号列表 |
| 构建集成 | 编辑器调用 `cargo build`、把错误显示成可点击列表的整套机制 |
