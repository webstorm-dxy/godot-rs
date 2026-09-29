# 02 · 新建项目与目录结构

## 1. 用项目管理器新建（推荐）

启动编辑器（不带 `--path`）会打开项目管理器：

```sh
./bin/godot.macos.editor.arm64.rust
```

在 **New Project** 对话框里：

1. 填项目名、选路径；
2. 勾选 **Rust Project**（在渲染器/版本控制那一行的右侧）；
3. 点 **Create & Edit**。

编辑器会立刻生成一个可运行的 Rust 工程，并在 `project.godot` 里写入
`rust/enabled=true`。

## 2. 命令行新建

```sh
./bin/godot.macos.editor.arm64.rust --headless --editor \
    --rust-init-project /path/to/MyGame --quit
```

（`--quit` 是必需的：命令只负责生成文件，加 `--quit` 才会退出。）

## 3. 生成的文件

```
MyGame/
├── project.godot                  # 含 [rust] enabled=true
├── src/
│   ├── lib.rs                     # crate 根：声明 mod、注册脚本
│   └── player.rs                  # 示例脚本（可挂到任意 Node2D）
├── Cargo.toml                     # 依赖引擎内置的 godot / godot-script
├── .cargo/config.toml             # 指定 target-dir 与绑定 JSON 路径
├── .gitignore                     # 忽略 .godot/
└── .godot/
    └── rust/
        ├── rust.gdextension       # 自动生成的扩展配置（含 entry_symbol）
        ├── bindings/              # 由引擎导出的 extension_api.json 等
        └── target/                # cargo 构建产物（.dylib 在这里）
```

**项目里没有需要手工维护的 `.gdextension` 文件**：配置生成在 `.godot/` 里，由模块
自己加载。

## 4. 两个文件的分工

`src/lib.rs`（crate 根）：

```rust
use godot::prelude::*;

mod player;                       // 每个脚本文件一行 mod

struct RustGame;

#[gdextension]
unsafe impl ExtensionLibrary for RustGame {
    fn on_stage_init(stage: InitStage) {
        if stage == InitStage::Scene {
            register_scripts();   // 引擎加载扩展时注册所有脚本
        }
    }
}

pub fn register_scripts() {
    player::register();           // 每个脚本文件一行注册
}
```

`src/player.rs`（脚本，见下一篇文档）。

> 在编辑器里**新建脚本**时，上面两行（`mod` 与 `register`）会自动加进 `src/lib.rs`，
> 不需要手写。
>
> 老项目（在这套功能之前创建的）在打开编辑器时也会被自动补齐：`src/lib.rs` 的 `mod`、
> `register` 调用，以及 `Cargo.toml` 里缺失的 `godot` / `godot-script` 依赖。
>
> 同时，如果 `godot` 指向的是 crates.io 的版本（会和 `godot-script` 用上两份
> `godot-core`，编译报 E0277/E0053），构建前会自动改成引擎里的 vendored 副本；
> 你写的额外 feature 会保留。细节见 [05 · 常见问题](05-troubleshooting.md)。

## 5. 常用设置

项目设置（Project Settings）：

| 设置 | 默认 | 说明 |
| --- | --- | --- |
| `rust/enabled` | `false` | 项目是否启用 Rust（新建时自动打开） |
| `rust/crate_root` | `res://` | Cargo 工程位置；`src/a.rs` 对应 `res://src/a.rs` |
| `rust/build/profile` | `debug` | `debug` 或 `release` |
| `rust/build/before_playing` | `true` | 按 F5 前自动构建 |
| `rust/build/on_editor_startup` | `true` | 打开编辑器时若库过期就构建 |
| `rust/build/extra_flags` | 空 | 追加给 `cargo build` 的参数 |

编辑器设置（Editor Settings）：

| 设置 | 说明 |
| --- | --- |
| `rust/cargo_path` | 指定 cargo 可执行文件（默认自动查找 PATH 与 `~/.cargo/bin`） |
| `rust/vendor_dir` | 指定引擎内置 gdext 的位置 |
| `rust/skip_build_before_playing` | 排查问题时临时跳过自动构建 |
