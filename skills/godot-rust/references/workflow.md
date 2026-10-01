# 工作流：脚手架 / 构建 / 编辑器 / 导出 / 调试 / 排错

来源：`docs/rust/01–10`、`modules/rust/README.md` 与实现代码；命令与报错均为原文。
**必须使用带 Rust 模块的引擎二进制**：`bin/godot.macos.editor.arm64.rust`
（`--version` 里含 `rust`；不带 `.rust` 后缀的那个二进制没有 Rust 模块）。

## 1. 脚手架与目录布局

```sh
# 新建/刷新（--quit 必需）
./bin/godot.macos.editor.arm64.rust --headless --editor --rust-init-project /path/to/MyGame --quit
# 刷新本项目绑定 JSON
./bin/godot.macos.editor.arm64.rust --headless --editor --path /path/to/MyGame --rust-regenerate-bindings --quit
```

成功日志：`Rust project created at '<dir>' (crate: <name>).`，失败返回 1。
重新脚手架会覆盖 `Cargo.toml`、`src/lib.rs`、`src/player.rs`、`.cargo/config.toml`、
`.godot/rust/rust.gdextension`（`.gitignore` 仅在缺失时创建），并把 `[rust] enabled = true` 写进 `project.godot`。

```text
MyGame/
├── project.godot          # [rust] enabled=true
├── Cargo.toml             # crate-type = ["cdylib"]，依赖引擎内置的 godot / godot-script
├── .cargo/config.toml     # target-dir 与 GDRUST_* 绑定路径
├── src/lib.rs             # mod 声明 + register_scripts()
├── src/player.rs          # 示例脚本
└── .godot/rust/           # rust.gdextension + bindings/ + target/（.dylib 在这里）
```

项目里**不需要手工维护 `.gdextension`**，模块自己从 `.godot/rust/` 加载。

## 2. Cargo.toml 与绑定（不要乱改）

```toml
[package]
name = "<项目名净化后>"          # 小写，非 [a-z0-9_] 变 _，空或以数字开头则 rust_game
version = "0.1.0"
edition = "2024"
rust-version = "1.94"           # 内置 gdext 0.5.5 的 MSRV

[lib]
crate-type = ["cdylib"]

[dependencies]
godot = { path = "<引擎>/modules/rust/vendor/gdext/godot", features = ["api-custom-json"] }
godot-script = { path = "<引擎>/modules/rust/support/godot-script" }
```

- **不要把 `godot` 指回 crates.io**，也不要指定别的 gdext 版本：会出现两份 `godot-core`，
  报 `only classes registered with Godot are allowed` / `E0053` / `cannot find crate godot_script`。
  额外 feature（如 `experimental-godot-api`）可以加，编辑器同步时会保留。
- 绑定必须来自**这套引擎**：编辑器把 `extension_api.json` / `gdextension_interface.json`
  写到 `.godot/rust/bindings/`，通过 `GDRUST_GODOT_API_JSON` / `GDRUST_GODOT_INTERFACE_JSON` 传给 gdext。

## 3. 构建

```sh
cd <project> && cargo build                      # 必须在项目目录里跑
./bin/godot.macos.editor.arm64.rust --headless --path <project> --build-solutions --quit   # 编辑器同款
```

- ⚠️ 从别处 `cargo build --manifest-path <project>/Cargo.toml` 会失败
  （`failed to run custom build command for godot-codegen`）：`.cargo/config.toml` 是按
  **当前目录**发现的，不在项目目录里跑就拿不到 `target-dir` 和 `GDRUST_*`。要跨目录就用
  `--config` 显式传，或直接让编辑器构建。
- 产物：`.godot/rust/target/debug/lib<crate>.dylib`（release 在 `release/`）。
- 触发方式等价：run-bar 的 Rust 图标（tooltip `Build Rust project`）、**F5**（先构建，失败不启动）、
  Rust 底栏的 **Build** 按钮、`--build-solutions`、编辑器启动时库过期自动构建。
- 失败信息：`cargo build failed. See the Rust output panel for details.`，
  Rust 面板 Problems 页双击可跳到 `.rs` 行；headless/CI 下 cargo 输出直接打到控制台。
- 首次构建需要网络（crates.io + 上游 `godot4-prebuilt`）；可先 `cargo fetch` 预热，
  之后用 `CARGO_NET_OFFLINE=true` 离线构建。

## 4. 运行与验证

```sh
./bin/godot.macos.editor.arm64.rust --headless --path <project> --quit-after 120
```

- 启动日志里每个生效的脚本都会打印：
  `godot-script: registered 'Player' (base Node2D) for res://src/player.rs`。
  没有这行 = 注册链断了。
- 属性/方法/信号的清单来自描述符缓存 `.godot/rust/script_cache.json`；删掉 `.godot/` 后要重新 Build。
- 参考 e2e：`tests/rust/e2e.sh`（脚手架 → `--build-solutions` → 运行 → 校验属性/方法/默认参数/信号）。

## 5. 编辑器工作流与热重载

- 附加脚本：节点右键 **Attach Script…** → Language **Rust** → 类名 + 路径。编辑器会按基类补
  `use godot::classes::<基类>;`，并把 `mod` / `register()` 写进 `src/lib.rs`。
- **每次改 Rust 代码都要重新构建**：编辑器加载的是 `.dylib`。
- 构建成功后脚本会**热重载**（日志 `Rust library reloaded: ...`）：运行中的实例会就地迁移
  （只迁移导出属性的值；私有字段/static 不迁移；`ready()` 不会重跑，占位实例升级除外），
  旧库不会被卸载。
- **需要重启编辑器**的情况：项目里用了 `#[derive(GodotClass)]` 节点类型
  （日志 `Node types (#[derive(GodotClass)]) need an editor restart`）、改了
  `src/lib.rs` 的 `on_stage_init` 入口、或要让**已存在**的实例切换新代码。
- 挂载失败提示：`Rust: script '%s' extends '%s', so it cannot be attached to an object of type '%s'.`

## 6. 设置

项目设置：`rust/enabled`（false）、`rust/crate_root`（`res://`）、`rust/build/profile`（debug）、
`rust/build/before_playing`（true）、`rust/build/on_editor_startup`（true）、
`rust/build/extra_flags`（""）、`rust/bindings_source`（vendored）、`rust/lsp/enabled`（true）。

编辑器设置：`rust/cargo_path`、`rust/vendor_dir`、`rust/rust_analyzer_path`、`rust/lldb_dap_path`、
`rust/skip_build_before_playing`（false）、`rust/show_output_panel_on_error`（true）。

环境变量：`GODOT_RUST_CARGO`（优先于设置与 PATH）、`GODOT_RUST_VENDOR_DIR`、
`GODOT_RUST_ANALYZER`、`CARGO_NET_OFFLINE`，以及被覆盖 `HOME` 时要显式给的 `RUSTUP_HOME`/`CARGO_HOME`。

## 7. 导出

- 导出预设（macOS / Windows / Linux）会在导出时按预设的 Debug/Release **重新 `cargo build`**，
  把动态库放到 `res://` 在导出后的位置（macOS 是 `App.app/Contents/Resources/`，其它平台在可执行文件旁），
  并把平台相关的 `.godot/rust/rust.gdextension` 打进 pck；日志
  `Rust: packaged lib<crate>.dylib (macos, release) into the export.`
- 跨平台导出会跳过并警告（需自行交叉编译后加进预设）；Android / iOS / Web 不支持。
- 导出后脚本不生效的排查：库是否存在、是否在 `res://` 解析到的目录、`--verbose` 下是否有
  `Rust extension loaded` / `godot-script: registered ...`、库与游戏是否同一次构建。

## 8. 调试（lldb-dap）

- 前置：macOS + Xcode 命令行工具（`xcode-select --install`）；找不到时设置
  编辑器设置 `rust/lldb_dap_path`，报错原文
  `lldb-dap was not found; install the Xcode command line tools or set rust/lldb_dap_path`。
- 用法：在 `.rs` 文件里打断点（编辑器自己的断点机制，随项目保存）→ Rust 面板 **Debug** 页 →
  **Debug**；会先 `cargo build`（debug profile）再在 `lldb-dap` 下启动游戏，支持
  Continue / Step Over / Into / Out、调用栈（含 Godot C++ 帧）与变量。
- 限制：只能启动调试，不能 attach；断点在会话开始后修改需重启会话；release 构建没有完整行信息。
- CI 驱动：`Engine.get_singleton("RustDebug")`，参考 `tests/rust/debug_e2e.sh`。

## 9. 报错对照表

编译期（来自派生宏）：

| 报错 | 原因 / 修法 |
| --- | --- |
| `add #[script(base = YourBaseNode)] to say which engine class this script extends` | `#[derive(RustScript)]` 缺 `#[script(base = ...)]` |
| `X is missing its script API block: add #[godot_script_api] impl X { ... } below the struct` | 缺 `#[godot_script_api]` impl 块 |
| `conflicting implementations of trait godot_script::RustScript for type X` | 派生宏与手写 `impl RustScript` 混用 |
| `a property can only have one hint` | 一个 `#[export]` 写了两个提示 |
| `parameters with #[opt(default = ...)] must come last` | 可选参数后还有必填参数 |
| `mismatched types: expected GString, found &str` | `default = ""` 与字段类型不符，用 `GString::from("")` |
| `type cannot be used as a property`（配 `Memory == MemRefCounted` 之类） | 导出裸 `Gd<T>`；改用 `Option<Gd<T>>` |
| `cannot find module or crate godot_script` | `Cargo.toml` 缺 `godot-script` 依赖（打开一次编辑器会自动补） |
| `only classes registered with Godot are allowed` / `E0053` | `godot` 指向了 crates.io 版本，改回引擎内置路径 |
| `unexpected cfg condition name: published_docs` | 内置 gdext 的告警，忽略 |

运行期：

| 现象 | 原因 / 修法 |
| --- | --- |
| `ERROR: Cannot get class 'Player'` | 库还没构建成功（占位实例）；按 F5 / Build |
| `ScriptInstance borrow failed, already bound` | 在脚本回调里改了**自己节点**的子节点；用 `call_deferred("add_child", ...)` |
| 脚本挂了但什么都没发生 | 检查 `register_script!` / `mod` / `register()` / 启动日志的 `registered` 行 |
| 检视面板没有属性 | 描述符来自 Rust；确认 `#[export]`、构建成功、`script_cache.json` 存在 |
| `Method 'RustScriptRegistry.register_script_type' has changed and no compatibility fallback has been provided` | 库是用另一套引擎构建的；重新构建引擎后 Build（必要时删 `.godot/rust/bindings` 与 `target`） |
| `Could not launch cargo...` | 找不到 cargo；设置 `rust/cargo_path` 或 `GODOT_RUST_CARGO` |
| 首次构建卡在 `Updating crates.io index` | 需要网络；先 `cargo fetch` |
| `RustScriptRegistry is missing. Is the engine built with the rust module enabled?` | 用错二进制（要用 `.rust` 后缀那个） |
