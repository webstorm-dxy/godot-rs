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

## `error[E0433]: cannot find module or crate godot_script`

项目是在这套模块支持「可挂载脚本」之前创建的，`Cargo.toml` 里只有 `godot`，没有
`godot-script` 依赖。

**现在打开一次编辑器就会自动补上**（模块会扫描 `src/*.rs`，发现用到 `godot::` /
`godot_script::` 且清单里缺依赖时写入，日志会出现 `Rust: added the missing dependencies
to Cargo.toml.`）。

也可以手动加（注意路径按你的引擎位置调整）：

```toml
[dependencies]
godot = { path = "<引擎>/modules/rust/vendor/gdext/godot", features = ["api-custom-json"] }
godot-script = { path = "<引擎>/modules/rust/support/godot-script" }
```

顺带一提：同一个机制也会补 `godot`（如果清单里连它都缺）。

### `error[E0277]: only classes registered with Godot are allowed` / `error[E0053]` / `cannot find crate godot_script`

`Cargo.toml` 里的 `godot` 指向了 **crates.io 上的 0.5**，而 `godot-script` 指向本引擎的
目录。这样会同时引入两份 `godot-core`，trait 对不上，于是出现一堆 E0277/E0053。

绑定必须来自**这套引擎**（扩展 API 版本要和引擎一致），所以编辑器会在构建前把这一行
改写成引擎里的 vendored 副本：

```toml
godot = { path = "<引擎>/modules/rust/vendor/gdext/godot", features = ["api-custom-json"] }
```

日志里会出现 `Rust: pointed the godot dependency at this engine's bindings.`。
你自己写的额外 feature / `default-features` / `optional` 会被保留，
只替换依赖来源，例如原来写的是：

```toml
godot = { version = "0.5", features = ["api-custom-json", "experimental-godot-api"] }
```

改完后是：

```toml
godot = { path = "<引擎>/modules/rust/vendor/gdext/godot", features = ["api-custom-json", "experimental-godot-api"] }
```

**不要**把 `godot` 改回 crates.io 版本，也不要手动指定别的 gdext 版本。

### 构建时出现 `unexpected \`cfg\` condition name: \`published_docs\``

来自 vendored 的 gdext 上游代码（`godot` / `godot-cell`），只是警告，不影响构建与运行，
可以忽略。它默认被 `[workspace.lints]` 的 `check-cfg` 关掉，作为路径依赖被引用时会漏出来。

### `Method 'RustScriptRegistry.register_script_type' has changed and no compatibility fallback has been provided`

项目里的 Rust 库是用**另一份引擎**构建的：GDExtension 用“方法签名哈希”查找引擎方法，
签名对不上就会报这个错（后面通常跟一个 gdext panic）。

新版本已经做了两件事：

1. 模块不再改动已发布方法的签名（描述符改用 `set_script_descriptor` 单独传递），
   旧库仍然能加载；
2. 库比 `.godot/rust/bindings/extension_api.json` 旧时会被判定为过期，打开编辑器会
   自动重新构建。

如果还是遇到：重新 `./build-macos.sh` 编译引擎，然后打开项目等它自动构建（或点 Build）。
实在不行就删掉项目里的 `.godot/rust/bindings` 与 `.godot/rust/target` 再构建一次。

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

脚本（`.rs` 挂载的类）在构建后会**热重载**：日志出现 `Rust library reloaded` 就说明新代码已经
生效，重新打开场景即可看到新的属性/方法/信号；运行游戏始终用最新库。

以下情况仍需重启编辑器（或 `Project → Reload Current Project`）：

- 项目里用了 `#[derive(GodotClass)]` 节点类型（日志会提示 `Node types (#[derive(GodotClass)]) need an editor restart`）；
- 改了 `src/lib.rs` 里的扩展入口（`on_stage_init` 注册逻辑）；
- 需要把**已经存在的**实例换成新代码（旧实例保持创建时的代码）。

### `Rust: editor icon 'BuildRust' could not be resolved.`

运行栏图标的主题项没找到，通常是没有完整重编译（图标在引擎编译时打包进主题）。
重新执行 `./build-macos.sh`，并确认 `modules/rust/icons/*.svg` 存在。

### 面板里没有 Problems / Output

Rust 面板在底部 Dock 的标签里（可能被折叠）；`rust/show_output_panel_on_error` 控制
构建失败时是否自动展开。

## rust-analyzer 报“不在模块树里”或语法错误

```
This file is not included anywhere in the module tree, so rust-analyzer can't offer IDE services.
Syntax Error: expected SEMICOLON
```

原因有两个，都已在新版本里修掉：

1. **文件没有加进 crate 根**：打开一次编辑器（或保存脚本）就会自动往 `src/lib.rs` 补
   `mod <name>;` 与 `<name>::register();`；旧的 `lib.rs` 无需手改。
2. **文件名/类名不是合法的 Rust 标识符**：新建脚本时类名会由文件名自动转成 PascalCase，
   但**已经生成过**的文件仍需自己改。把结构体名改成合法类型名（例如
   `pub struct my-script` → `pub struct MyScript`），文件名改成下划线形式
   （`my-script.rs` → `my_script.rs`）后重新打开编辑器。

判断方法：`src/lib.rs` 里应能看到 `mod my_script;`，`src/my_script.rs` 里应是
`pub struct MyScript` 加 `godot_script::register_script!(MyScript);`。

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

属性来自 Rust 侧的描述符，不解析源码，所以先确认：

1. `properties()` 里声明了该属性，`get_property` 能返回它；
2. 构建成功过（构建后描述符会写进 `.godot/rust/script_cache.json`，以后即使库还没加载
   也能显示类名/属性/方法/信号）；
3. 如果缓存文件被删了（例如清空了 `.godot/`），先按一次 Build 或 F5，让描述符重新生成；
4. 库重新构建后脚本会热重载（见上一节）；只有 `#[derive(GodotClass)]` 节点类型需要重启编辑器。

## 引擎开发相关

### 编译报 `could not build module 'std_float_h'`

`PATH` 里 Homebrew LLVM 抢在 Apple 工具链前面。用 `./build-macos.sh`（已内置修正），
或手动 `export PATH="/Library/Developer/CommandLineTools/usr/bin:$PATH"`。

### 手动 scons 时模块没编进去

加 `module_rust_enabled=yes`，并确认产物名带 `.rust` 后缀。
