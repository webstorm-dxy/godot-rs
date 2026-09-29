# 05 · 常见问题与排查

## 构建相关

### `Rust: RustScriptRegistry is missing. Is the engine built with the rust module enabled?`

引擎没有编译 Rust 模块，或者用的是旧的二进制。确认产物是
`bin/godot.macos.editor.arm64.rust`，并检查 `--version` 里是否有 `rust`。

### `Could not launch cargo. Install the Rust toolchain or set rust/cargo_path…`

- `cargo --version` 是否可用；
- 从 Finder 启动编辑器时 `PATH` 可能不含 `~/.cargo/bin`：在编辑器设置里填 `rust/cargo_path`，
  或设置环境变量 `GODOT_RUST_CARGO=/完整/路径/cargo`。

### `rustup could not choose a version of cargo to run …`

`rustup` 找不到工具链，通常是 `HOME`/`RUSTUP_HOME` 被改了（例如脚本里改了 `HOME`）。
设置 `RUSTUP_HOME`（默认 `~/.rustup`）与 `CARGO_HOME` 即可。

### 第一次构建很慢 / 卡在 `Updating crates.io index`

第一次要下载依赖与上游 `godot4-prebuilt` 仓库；网络不通时会一直重试。
先在项目目录执行 `cargo fetch` 预热，或配置代理/镜像。

### `error: rustup could not choose a version of cargo` / 离线构建

`CARGO_NET_OFFLINE=true` 可以让 cargo 完全用本地缓存（前提是已经 `cargo fetch` 过）。

## 编辑器相关

### 创建 Rust 脚本时出现下面三条之一

```
ERROR: delimiter must start with a symbol
ERROR: auto brace completion open key must be a symbol
ERROR: delimiter with start key '//!' already exists.
```

这是**旧二进制**的分隔符表问题（Rust 的 `r#"…"#` 前缀是字母、`//!` 重复），已在
`modules/rust/rust_language.cpp` 修复。重新编译引擎并重启编辑器即可。

### `ERROR: Cannot get class 'Player'`

场景里的 Rust 类还没注册：库没构建（先 F5/点 Build），或场景在构建完成前就打开了。
构建成功后模块会刷新**未修改**的已打开场景；有未保存改动时请手动重开场景。

### 构建成功，但编辑器里看不到新类 / 改了类名没生效

编辑器内的类信息需要重新加载扩展或重启编辑器。运行游戏不受影响（总是用新库）。

### `Rust: editor icon 'BuildRust' could not be resolved.`

运行栏图标的主题项没找到，通常是没有完整重编译（图标在引擎编译时打包进主题）。
重新执行 `./build-macos.sh`，并确认 `modules/rust/icons/*.svg` 存在。

### 面板里没有 Problems / Output

Rust 面板在底部 Dock 的标签里（可能被折叠）；`rust/show_output_panel_on_error` 控制
构建失败时是否自动展开。

## 脚本行为相关

### 脚本挂上去了，但运行时什么都没发生

按顺序检查：

1. 文件里是否有 `godot_script::register_script!(Player);`；
2. `src/lib.rs` 里是否有 `mod player;` 和 `player::register();`；
3. `CLASS_NAME` 与 `BASE_NAME` 是否和 `type Base` 对应；
4. 构建输出里有没有 `godot-script: registered 'Player' (base Node2D) for res://src/player.rs`。

### `Rust: script ... extends ... so it cannot be attached to an object of type ...`

脚本的基类和节点类型不匹配（和 GDScript/C# 的规则一致）：比如 `type Base = Node2D`
的脚本不能挂到 `Control` 上。改基类或换节点。

### 检视面板里看不到属性

确认 `properties()` 里声明了该属性、`get_property` 能返回它，并且**构建过**（属性来自
Rust 侧注册表，不是解析源码得到的）。

## 引擎开发相关

### 编译报 `could not build module 'std_float_h'`

`PATH` 里 Homebrew LLVM 抢在 Apple 工具链前面。用 `./build-macos.sh`（已内置修正），
或手动 `export PATH="/Library/Developer/CommandLineTools/usr/bin:$PATH"`。

### 手动 scons 时模块没编进去

加 `module_rust_enabled=yes`，并确认产物名带 `.rust` 后缀。
