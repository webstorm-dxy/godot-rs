# Godot Engine for Rust

> **Godot 4.7.2 的 Rust 原生适配分支** —— Rust 在这里不是外挂的 GDExtension，而是引擎内置的一等语言。

一个 `.rs` 文件就是一份可以挂到任意节点上的脚本，写法和 GDScript 对齐；编辑器替你完成脚手架、
`cargo build`、报错跳转、代码补全、热重载、断点调试和导出打包。

| 项目 | 说明 |
| --- | --- |
| 引擎版本 | `4.7.2.stable`（见 `version.py`） |
| 编辑器产物 | `bin/godot.macos.editor.arm64.rust`（模块后缀 `.rust`，与 C# 的 `.mono` 同理） |
| 已验证平台 | macOS (Apple Silicon)；Linux / Windows 代码路径兼容，但未实测 |
| 内置绑定 | godot-rust `gdext 0.5.5`（保持上游原样，MPL-2.0） |
| 中文文档 | [docs/rust/](docs/rust/README.md)，含[手把手教程](docs/rust/10-tutorial.md) |
| 模块技术说明 | [modules/rust/README.md](modules/rust/README.md) |

## 相比传统 godot-rust (gdext) 用法，新在哪里

| 维度 | 传统 gdext 用法 | 本分支的适配 |
| --- | --- | --- |
| 脚本形态 | 每个类都要 `#[derive(GodotClass)]`，自己定义节点类型 | `.rs` 直接作为脚本挂到**已有节点**上，像 `.gd` 一样 |
| 扩展配置 | 每个项目手工维护 `.gdextension` 文件 | 模块自动生成并加载 `.godot/rust/rust.gdextension` |
| 绑定来源 | 依赖 crates.io 上预编译的绑定 | 从**当前这份引擎**导出 `extension_api.json` 生成绑定 |
| 构建 | 自己在项目里跑 `cargo build` | 编辑器接管：F5 之前、编辑器启动时、运行栏按钮、`--build-solutions` |
| 编译报错 | 只能看终端 | Problems 面板可点击，双击跳到 `.rs` 对应行 |
| 补全跳转 | 自己配 rust-analyzer | 编辑器内置 rust-analyzer 客户端 |
| 改完代码 | 重启编辑器 | 热重载，运行中的实例就地迁移状态 |
| 断点调试 | 自己接 lldb / gdb | 内置 lldb-dap 客户端，直接在 `.rs` 里打断点 |
| 导出 | 自己处理动态库 | 导出插件按预设构建，并把库打进产物 |

一句话：**gdext 解决「Rust 能调用引擎」，这个模块解决「引擎把 Rust 当自己人」。**

## 新适配总览

| 领域 | 新增能力 | 主要实现 |
| --- | --- | --- |
| 脚本系统 | `.rs` 识别为 `RustScript` 资源，可挂载、可序列化、参与资源扫描 | `rust_language.*`、`rust_script.*` |
| 派生宏 | `#[derive(RustScript)]` + `#[godot_script_api]` 生成全部胶水代码 | `support/godot-script-derive/` |
| 运行时 API | `RustScript` trait 与 gdext 的 `ScriptInstance` 实现 | `support/godot-script/` |
| 工程脚手架 | 一键生成 Cargo 工程；自动维护 `mod` / `register()`；自动补依赖 | `editor/rust_project.*` |
| 构建集成 | 后台 `cargo build` + JSON 诊断 + 可点击错误 | `editor/rust_build.*` |
| 编辑器 UI | Rust 底部面板（Problems / Output / Debug）、运行栏 Build 按钮、Rust 图标 | `editor/rust_build_panel.*`、`icons/` |
| 语法高亮 | 主题感知的 `.rs` 高亮，且不依赖 rust-analyzer 也能补全 | `editor/rust_highlighter.*` |
| 语言服务器 | 内置 rust-analyzer：补全、悬停、跳转定义、实时诊断 | `editor/rust_lsp.*` |
| 热重载 | 构建成功即加载新库，运行中的实例就地换过去 | `RustScript::migrate_instances()` |
| 调试 | lldb-dap 断点、单步、调用栈、变量 | `editor/rust_debug.*` |
| 导出 | 按预设构建，动态库 + 平台专用配置一起进产物 | `editor/rust_export_plugin.*` |
| 绑定 | 运行时导出本引擎的 API，内置 gdext 免 patch | `editor/rust_bindings.*`、`vendor/gdext/` |

## 可挂载脚本：`.rs` 就是脚本

这是整套适配里最核心的一条：**你写的 `.rs` 文件本身就是脚本资源**，直接挂到场景里已有的节点上，
不必为每段逻辑新建一个节点类型。

~~~rust
use godot::classes::Node2D;
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D)]
pub struct Player {
    /// 名字为 owner 的字段会自动拿到脚本挂载的那个节点。
    owner: Gd<Node2D>,

    /// 导出到检视面板，并随场景一起保存。
    #[export]
    speed: f32,

    /// 默认值不是 Default::default() 时用 default；range 会变成滑块提示。
    #[export(default = 9.8, range = (0.0, 100.0, 0.5))]
    gravity: f32,
}

#[godot_script_api]
impl Player {
    // 生命周期钩子：按名字识别，签名固定，不要加 #[func]。
    fn ready(&mut self) {
        godot_print!("Player ready");
    }

    fn process(&mut self, delta: f64) {
        let _ = self.speed * delta as f32;
    }

    // 暴露给 GDScript 与编辑器的方法。
    #[func]
    fn jump(&mut self, height: f32) -> f32 {
        self.speed + height
    }

    // #[opt] 参数在 GDScript 侧可以省略。
    #[func]
    fn boost(&mut self, amount: f32, #[opt(default = 1.0)] factor: f32) -> f32 {
        amount * factor
    }

    // 声明信号，GDScript 侧可以 connect()。
    #[signal]
    fn jumped(height: f32) {}
}

godot_script::register_script!(Player);
~~~

GDScript 侧就像调用一个 `.gd` 脚本：

~~~gdscript
func _ready() -> void:
	var player: Node2D = $Player
	print(player.has_method("jump"))      # true
	player.set("speed", 7.0)              # 导出属性
	print(player.call("jump", 5.0))       # 12.0
	player.connect("jumped", _on_jumped)  # 信号
~~~

| 写法 | 效果 |
| --- | --- |
| `owner: Gd<Node2D>` | 名为 `owner` 的字段自动拿到脚本挂载的那个节点（只是引用） |
| `#[script(base = Node2D, tool, name)]` | 声明基类（引擎校验 `is-a`）；`tool` 表示编辑器里也运行；`name` 自定义类名 |
| `#[export]` 字段 | 进入检视面板、随场景保存、出现在 `get_script_property_list()` |
| `#[export(default = ..., range = ...)]` | 声明默认值（检视面板的「恢复默认」）与滑块等提示，另支持 `enum` / `flags` / `file` / `dir` / `multiline` / `placeholder` / `node` / `color_no_alpha` / `exp_easing` / `storage` |
| `#[func]` 方法 | GDScript 可 `has_method()` / `call()`，编辑器也可调用 |
| `#[opt(default = ...)]` 参数 | 该参数在 GDScript 侧可以省略 |
| `#[signal]` 声明 | GDScript 可 `has_signal()` / `connect()`，Rust 侧 `emit_signal()` |
| `ready / process / physics_process / enter_tree / exit_tree` | 按名字识别的生命周期钩子，签名固定，不加 `#[func]` |
| `register_script!(T)` | 把类名、基类、属性、方法、信号注册给引擎 |

行为上和 GDScript 对齐的地方：

- `has_method()`、`call()`、`get_method_list()`、`has_signal()`、`get_signal_list()`、`get_script_*_list()` 都按注册的描述符回答；
- 从编辑器新建脚本时会自动导入基类，并把 `mod` / `register()` 写进 `src/lib.rs`，不用手改；
- 只有 tool 脚本会在编辑器里实例化，普通脚本只在游戏里运行；
- 库还没构建时挂脚本不会报错：编辑器用**占位实例**只读显示属性，构建成功后自动生效。

## 工程脚手架与构建闭环

Rust 工程既可以一键生成，也可以由编辑器持续维护。项目管理器里有 **Rust Project** 开关，命令行等价写法：

~~~sh
godot --headless --editor --rust-init-project /path/to/project --quit
~~~

它会写出 `Cargo.toml`、`src/lib.rs`、`src/player.rs`、`.cargo/config.toml`、`.gitignore`
和 `.godot/rust/rust.gdextension`。

之后编辑器会：

- **保持 crate 健康**：每个 `src/*.rs` 自动获得 `mod` / `register()`；`Cargo.toml` 的依赖被指向本引擎内置的 `godot` 与 `godot-script`，避免 crates.io 的版本再拉进第二份 `godot-core`；
- **自动构建**：调用 `cargo build --message-format=json-render-diagnostics`，触发点包括 F5 之前、`--build-solutions`、编辑器启动时发现库缺失或比源码旧、运行栏 **Build** 按钮与 Rust 面板里的构建按钮；
- **收集诊断**：cargo 的错误进入 Rust 面板（Problems / Output）和编辑器日志，双击直接跳到 `.rs` 行；headless 运行时把 cargo 输出镜像到控制台，所以 CI 里可以直接用 `--build-solutions`；
- **加载动态库**：直接通过 `GDExtensionManager` 加载，各种构建模式一致。配置放在 `.godot/` 下、不在项目树里，因此模块自己加载它，也不去改写引擎的 `extension_list.cfg`；没构建过时保持安静，而不是刷一屏加载错误。

## 编辑器体验

- **语法高亮**：`.rs` 在编辑器里按主题着色（关键字、类型、宏、数字、字符串、注释、文档注释、属性、引擎类，以及项目自己的脚本类），在高亮器菜单中显示为 “Rust”。
- **不依赖 rust-analyzer 的补全**：语言本身提供关键字、类型、宏、代码片段、当前文件的函数，以及 `self.` 之后的脚本成员。
- **Rust 底部面板**：Problems / Output / Debug 三页；构建失败时可以自动展开。
- 运行栏的 **Build** 按钮使用 Rust 图标，`.rs` 在文件系统里也有自己的图标。
- 新建脚本、切换脚本、缓存过期等边界情况都有处理，`.rs` 高亮不会因为脚本编辑器缓存陈旧而丢失。

## 内置 rust-analyzer

模块会为 crate 启动一个真正的 `rust-analyzer` 子进程，把**补全、悬停、跳转定义、实时诊断**
转发给内置脚本编辑器；二进制缺失或中途退出时**快速失败**并带上捕获的 stderr，而不是卡在第一个请求上。

`rust/rust_analyzer_path` 可以指定二进制，环境变量 `GODOT_RUST_ANALYZER` 优先级更高；
不想用时把 `rust/lsp/enabled` 设为 `false`。细节见 [07-language-server.md](docs/rust/07-language-server.md)。

## 热重载

构建成功之后：

1. 把新库快照到 `.godot/rust/reload/<n>/`，生成指向它的 `.gdextension`；
2. 通过 `GDExtensionManager` 加载这份快照，Rust 侧重新注册脚本类型（按 `res://` 路径覆盖描述符）；
3. 把**运行中的实例就地迁移**过去：读出旧实例的导出属性值，写进新库创建的实例，再替换 `script_instance`；此前只是占位的实例还会补一次 `ready()`。

边界要清楚：

- **旧库不会卸载**（Rust 无法安全卸载已加载的代码），它一直留到进程退出；
- 能迁移的是导出属性；私有字段、静态变量属于旧库，不会跟过去；
- 用 `#[derive(GodotClass)]` 注册的**节点类型**在同一进程里无法重复注册，这类项目仍然需要重启编辑器。

## 断点调试（lldb-dap）

`RustDebug` 是一个面向 `lldb-dap` 的最小 DAP 客户端：

- 断点就是编辑器自己的断点：在 `.rs` 里点行号，随项目一起保存；
- Rust 面板的 **Debug** 页启动会话，显示状态、调用栈和变量，支持继续 / 单步跳过 / 步入 / 步出；
- 被调试的是**游戏进程**，所以调用栈里会混着引擎的 C++ 帧，变量按原生布局显示。

`rust/lldb_dap_path` 可以指定 `lldb-dap`；默认在 `PATH`、Command Line Tools 和 Xcode 里探测。
见 [09-debugging.md](docs/rust/09-debugging.md)。

## 导出

导出桌面预设时，导出插件会：

- 按预设的 debug / release 重新构建 crate；
- 用 `add_shared_object()` 把动态库放到 `res://` 在导出产物中对应的位置（macOS 应用包里是 `Contents/Resources`，其他平台在可执行文件旁）；
- 往 pck 里写一份只含该平台的 `.godot/rust/rust.gdextension`。

于是导出的游戏加载的正是编辑器里跑过的那套脚本。**跨平台导出**（在 macOS 上导 Windows 等）
不会自动交叉编译，只会给出警告：需要自行交叉编译并把库放进去。
见 [08-exporting.md](docs/rust/08-exporting.md)。

## 引擎匹配的绑定与内置 gdext

- `extension_api.json` 与 `gdextension_interface.json` 由**正在运行的这份编辑器**导出到 `.godot/rust/bindings/`，gdext 通过 `api-custom-json` 特性消费它们（`GDRUST_GODOT_API_JSON` / `GDRUST_GODOT_INTERFACE_JSON`）。**改过引擎也不需要 patch 绑定 crate。**
- `modules/rust/vendor/gdext` 内置 godot-rust **0.5.5**（保持上游原样，MPL-2.0）；`GODOT_RUST_VENDOR_DIR` 或编辑器设置 `rust/vendor_dir` 可覆盖位置，缺失时回退到 crates.io 版本。
- `modules/rust/tools/sync_gdext.py` 用于重新 vendor 更新的上游 tag。
- `--rust-regenerate-bindings` 手动刷新绑定。

## 快速开始

前置：Rust 工具链（`cargo` / `rustc` **1.94+**，内置 gdext 0.5.5 的要求）、C++ 工具链、Python + SCons。

~~~sh
# 1) 编译带 Rust 模块的编辑器（macOS 便捷脚本，已打开 module_rust_enabled）
./build-macos.sh
#    产物：bin/godot.macos.editor.arm64.rust

# 2) 生成一个 Rust 项目（等价于项目管理器里勾选 "Rust Project"）
./bin/godot.macos.editor.arm64.rust --headless --editor --rust-init-project /path/to/MyGame --quit

# 3) 打开编辑器，改 src/player.rs，按 F5（会先 cargo build 再运行）
./bin/godot.macos.editor.arm64.rust --path /path/to/MyGame -e
~~~

手动调用 scons 时记得带上模块开关：

~~~sh
scons platform=macos arch=arm64 target=editor module_rust_enabled=yes
~~~

版本串里出现 `rust` 就说明模块已经编进去了：

~~~sh
./bin/godot.macos.editor.arm64.rust --headless --version
# 4.7.2.stable.rust.custom_build.xxxxxxxx
~~~

CI / headless：

~~~sh
godot --headless --path /path/to/project --build-solutions --quit
~~~

用户项目**首次构建需要网络**（cargo 要从 crates.io 解析 gdext 的传递依赖）。想预热可以先在项目里跑一次 `cargo fetch`。

## 设置项

**项目设置**（`project.godot`）：

| 设置 | 默认 | 含义 |
| --- | --- | --- |
| `rust/enabled` | `false` | 标记该项目启用 Rust（加上 `Rust` 特性标签） |
| `rust/crate_root` | `res://` | `Cargo.toml` 所在位置，用于把 cargo 路径映射回 `res://` |
| `rust/build/profile` | `debug` | `debug` 或 `release` |
| `rust/build/before_playing` | `true` | 运行前自动构建 |
| `rust/build/on_editor_startup` | `true` | 编辑器启动时发现库过期就构建 |
| `rust/build/extra_flags` | `""` | 追加给 `cargo build` 的参数 |
| `rust/bindings_source` | `vendored` | `vendored` 或 `crates-io` |
| `rust/lsp/enabled` | `true` | 编辑器运行期间启动 rust-analyzer |

**编辑器设置**：

| 设置 | 默认 | 含义 |
| --- | --- | --- |
| `rust/cargo_path` | `""` | 指定 `cargo`；否则探测 `PATH` 与 `~/.cargo/bin` |
| `rust/vendor_dir` | `""` | 指定内置 gdext 工作区的位置 |
| `rust/rust_analyzer_path` | `""` | 指定 `rust-analyzer` |
| `rust/lldb_dap_path` | `""` | 指定 `lldb-dap` |
| `rust/skip_build_before_playing` | `false` | 调试用逃生开关：运行前从不构建 |
| `rust/show_output_panel_on_error` | `true` | 构建失败时展开面板 |

**环境变量**：`GODOT_RUST_CARGO`、`GODOT_RUST_ANALYZER`、`GODOT_RUST_VENDOR_DIR`。

## 命令行参数

| 参数 | 作用 |
| --- | --- |
| `--rust-init-project <dir>` | 生成（或重新生成）Rust 工程 |
| `--rust-regenerate-bindings` | 刷新本引擎的 `extension_api.json` / `gdextension_interface.json` |
| `--build-solutions` | 构建工程（Rust 模块会执行 `cargo build`），之后编辑器继续运行 |

这两个 Rust 参数在模块初始化阶段处理，且**不会让引擎退出**，headless 下请配合 `--editor` 与 `--quit`：

~~~sh
godot --headless --editor --path /path/to/project --rust-regenerate-bindings --quit
godot --headless --editor --path /path/to/project --rust-init-project /path/to/project --quit
~~~

## 文档与测试

| 文档 | 内容 |
| --- | --- |
| [10-tutorial.md](docs/rust/10-tutorial.md) | **手把手教程**：从建项目到导出，做一个完整小游戏 |
| [01-installation.md](docs/rust/01-installation.md) | 编译编辑器与环境要求 |
| [02-project-setup.md](docs/rust/02-project-setup.md) | 新建项目、目录结构、设置项 |
| [03-writing-scripts.md](docs/rust/03-writing-scripts.md) | 写可挂载脚本：属性、方法、信号、生命周期 |
| [04-editor-workflow.md](docs/rust/04-editor-workflow.md) | 编辑器里的完整工作流 |
| [05-troubleshooting.md](docs/rust/05-troubleshooting.md) | 常见报错与排查 |
| [06-architecture.md](docs/rust/06-architecture.md) | 模块内部原理 |
| [07-language-server.md](docs/rust/07-language-server.md) | rust-analyzer 集成 |
| [08-exporting.md](docs/rust/08-exporting.md) | 导出 |
| [09-debugging.md](docs/rust/09-debugging.md) | 断点调试 |
| [modules/rust/README.md](modules/rust/README.md) | 模块范围与实现（英文） |

测试：

~~~sh
tests/rust/e2e.sh          # 脚手架 -> 构建 -> 运行 -> 绑定，端到端
tests/rust/debug_e2e.sh    # 调试链路
~~~

## 支持范围与路线图

**已实现**：项目脚手架与构建集成、诊断、扩展加载、可挂载脚本（派生宏、属性、方法、信号、默认参数）、
描述符缓存、tool 脚本、`.rs` 高亮与降级补全、rust-analyzer 集成、脚本热重载与实例迁移、
lldb-dap 断点调试、导出打包。

**尚未实现**：

| 项 | 说明 |
| --- | --- |
| RPC | `@rpc` 风格的多人同步 |
| 跨平台导出 | 在 macOS 上导 Windows / Linux 需要自行交叉编译 |

## 目录结构

~~~text
modules/rust/
├── rust_language.*              # ScriptLanguage：高亮、校验、LSP 挂载点
├── rust_script.*                # .rs 对应的 Script 资源（由描述符驱动）
├── rust_script_registry.*       # 引擎单例 RustScriptRegistry
├── rust_script_resource_format.*
├── rust_paths.*                 # .godot/rust 下的路径约定
├── rust_diagnostics.*           # 线程安全的诊断存储
├── editor/
│   ├── rust_project.*           # 路径、设置、脚手架、脚本缓存
│   ├── rust_bindings.*          # 引擎内导出 extension API + 缓存
│   ├── rust_build.*             # cargo 调用、JSON 诊断、线程
│   ├── rust_build_panel.*       # Rust 面板（Problems / Output / Debug）
│   ├── rust_highlighter.*       # .rs 语法高亮
│   ├── rust_lsp.*               # rust-analyzer 客户端
│   ├── rust_debug.*             # lldb-dap 客户端
│   ├── rust_export_plugin.*     # 把库与配置打进导出产物
│   └── rust_editor_plugin.*     # 运行栏按钮、构建回调、扩展加载
├── icons/                       # 随主题分发的编辑器图标
├── support/godot-script/        # 运行时 crate：RustScript trait、ScriptInstance
├── support/godot-script-derive/ # 过程宏：RustScript、godot_script_api
├── vendor/gdext/                # 内置 godot-rust 0.5.5（MPL-2.0）
└── tools/sync_gdext.py          # 同步更新的上游 tag
~~~

---

> 以下为上游 Godot Engine 的原版 README，未作改动。

# Godot Engine

<p align="center">
  <a href="https://godotengine.org">
    <img src="misc/logo/logo_outlined.svg" width="400" alt="Godot Engine logo">
  </a>
</p>

## 2D and 3D cross-platform game engine

**[Godot Engine](https://godotengine.org) is a feature-packed, cross-platform
game engine to create 2D and 3D games from a unified interface.** It provides a
comprehensive set of [common tools](https://godotengine.org/features), so that
users can focus on making games without having to reinvent the wheel. Games can
be exported with one click to a number of platforms, including the major desktop
platforms (Linux, macOS, Windows), mobile platforms (Android, iOS), as well as
Web-based platforms and [consoles](https://godotengine.org/consoles).

## Free, open source and community-driven

Godot is completely free and open source under the very permissive [MIT license](https://godotengine.org/license).
No strings attached, no royalties, nothing. The users' games are theirs, down
to the last line of engine code. Godot's development is fully independent and
community-driven, empowering users to help shape their engine to match their
expectations. It is supported by the [Godot Foundation](https://godot.foundation/)
not-for-profit.

Before being open sourced in [February 2014](https://github.com/godotengine/godot/commit/0b806ee0fc9097fa7bda7ac0109191c9c5e0a1ac),
Godot had been developed by [Juan Linietsky](https://github.com/reduz) and
[Ariel Manzur](https://github.com/punto-) for several years as an in-house
engine, used to publish several work-for-hire titles.

![Screenshot of a 3D scene in the Godot Engine editor](https://raw.githubusercontent.com/godotengine/godot-design/master/screenshots/editor_tps_demo_1920x1080.jpg)

## Getting the engine

### Binary downloads

Official binaries for the Godot editor and the export templates can be found
[on the Godot website](https://godotengine.org/download).

### Compiling from source

[See the official docs](https://docs.godotengine.org/en/latest/engine_details/development/compiling)
for compilation instructions for every supported platform.

## Community and contributing

Godot is not only an engine but an ever-growing community of users and engine
developers. The main community channels are listed [on the homepage](https://godotengine.org/community).

The best way to get in touch with the core engine developers is to join the
[Godot Contributors Chat](https://chat.godotengine.org).

To get started contributing to the project, see the [contributing guide](CONTRIBUTING.md).
This document also includes guidelines for reporting bugs.

## Documentation and demos

The official documentation is hosted on [Read the Docs](https://docs.godotengine.org).
It is maintained by the Godot community in its own [GitHub repository](https://github.com/godotengine/godot-docs).

The [class reference](https://docs.godotengine.org/en/latest/classes/)
is also accessible from the Godot editor.

We also maintain official demos in their own [GitHub repository](https://github.com/godotengine/godot-demo-projects)
as well as a list of [awesome Godot community resources](https://github.com/godotengine/awesome-godot).

There are also a number of other
[learning resources](https://docs.godotengine.org/en/latest/community/tutorials.html)
provided by the community, such as text and video tutorials, demos, etc.
Consult the [community channels](https://godotengine.org/community)
for more information.

[![Code Triagers Badge](https://www.codetriage.com/godotengine/godot/badges/users.svg)](https://www.codetriage.com/godotengine/godot)
[![Translate on Weblate](https://hosted.weblate.org/widgets/godot-engine/-/godot/svg-badge.svg)](https://hosted.weblate.org/engage/godot-engine/?utm_source=widget)
