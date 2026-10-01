---
name: godot-rust
description: 用 Rust 为 Godot 写脚本（本仓库 godot-rs 内置的 modules/rust）。当任务涉及 RustScript、#[derive(RustScript)]、#[godot_script_api]、#[export]/#[func]/#[signal]、godot-script crate、--rust-init-project 脚手架、--build-solutions / cargo build 构建脚本库、把 Rust 逻辑挂到节点上、或排查脚本没生效／挂不上／属性不出现在检视面板时使用。也适用于用 Rust 写玩家、敌人、UI 等玩法代码。不适用于修改引擎 C++（modules/rust/*.cpp、core/、scene/），也不适用于普通 crates.io 的 gdext 项目。
---

# 用 Rust 写 Godot 脚本

本仓库是一个 Godot 4.7 分支，内置 `modules/rust` 模块：`.rs` 文件就是可挂载脚本，
用法贴近 GDScript。写代码前先确认目标：**给游戏项目写脚本**（本技能）还是**改引擎 C++**（不是）。

## 心智模型

1. **一个 `.rs` 文件 = 一个可挂载脚本类**，挂在**已有节点**上（不是新节点类型）。
2. **两个宏分工**：`#[derive(RustScript)]` 读**结构体**（基类、导出字段），
   `#[godot_script_api]` 读 **impl 块**（生命周期、`#[func]` 方法、`#[signal]` 信号）。
   两者缺一不可，且**不能和手写 `impl RustScript` 混用**。
3. **注册是显式的**，三处都要有，否则脚本静默失效（不报错、不运行）：
   文件里 `godot_script::register_script!(Player);`、`src/lib.rs` 里 `mod player;`
   和 `register_scripts()` 里的 `player::register();`。编辑器创建脚本时会自动补后两处。

## 最小可用脚本（`src/player.rs`）

```rust
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D)]          // 必填：脚本能挂到 Node2D 及其子类上
pub struct Player {
    owner: Gd<Node2D>,            // 名字为 owner 的字段自动接收挂载的节点
    #[export]                     // 显示在检视面板并保存进场景
    speed: f32,
    internal: i64,                // 不导出：普通 Rust 字段，默认 Default::default()
}

#[godot_script_api]
impl Player {
    fn ready(&mut self) {
        godot_print!("Player ready, speed = {}", self.speed);
    }

    #[func]                       // GDScript: player.call("jump", 5.0)
    fn jump(&mut self, height: f32) -> f32 {
        self.speed + height
    }

    #[func]
    fn do_jump(&mut self) {
        let height = 5.0;
        self.owner.clone().emit_signal("jumped", &[height.to_variant()]);
    }

    #[signal]                     // GDScript: player.jumped.connect(...)
    fn jumped(height: f32) {}
}

godot_script::register_script!(Player);
```

`src/lib.rs`（脚手架已生成，新增脚本要加两行）：

```rust
mod player;                       // ① 声明模块
pub fn register_scripts() {
    player::register();           // ② 注册脚本
}
```

## 硬性规则

- **基类不在 prelude 里就要 import**。`godot::prelude` 只有
  `Node, Node2D, Node3D, Object, RefCounted, Resource, PackedScene, SceneTree` 等少数类型；
  `CharacterBody2D`、`Control`、`Area2D`、`Timer`、`Sprite2D` 等必须
  `use godot::classes::CharacterBody2D;`（Rust 拼写：`GPUParticles2D` → `GpuParticles2D`）。
- **生命周期方法按名字识别，签名必须精确**，且**不加** `#[func]`：
  `fn ready(&mut self)`、`fn process(&mut self, delta: f64)`、
  `fn physics_process(&mut self, delta: f64)`、`fn enter_tree(&mut self)`、`fn exit_tree(&mut self)`。
- **只有 `#[func]` 方法能被 GDScript/编辑器调用**；普通方法只在 Rust 内可见。
- **`#[opt(default = ...)]` 参数必须放在参数表最后**，否则编译报
  `parameters with #[opt(default = ...)] must come last`。
- **一个属性只能有一个提示**（`range`/`enum`/`file`/... 只能写一个），
  `default` 可以与提示共存，`storage` 不能与提示共存。
- **`default = ...` 表达式的类型必须是字段的 Rust 类型**：`GString` 字段要写
  `default = GString::from("hero")`，写 `default = ""` 会编译失败。
- **导出节点引用用 `Option<Gd<T>>`**：`#[export] node_ref: Option<Gd<Node2D>>` ✅；
  裸 `Gd<Node2D>` ❌（`type cannot be used as a property`）。
- **一个文件一个 `register_script!`**；辅助函数可以放同目录的其他模块。
- **文件名必须是合法 Rust 模块名**（小写加下划线，不能有 `-`、不能以数字开头），
  结构体名用 UpperCamelCase。

## 最容易踩的坑：回调里不要改自己节点的子节点

在 `ready`/`process`/`#[func]` 里对自己节点调用 `add_child`/`remove_child` 会触发
重入通知，游戏直接 panic：

```text
ScriptInstance borrow failed, already bound; T = ...::Player
  Details: cannot borrow while accessible mutable borrow exists.
```

**改成延迟调用**（已验证可行），或加到别的节点上（安全）：

```rust
// ❌ self.owner.clone().add_child(&bullet);
// ✅
self.owner.clone().call_deferred("add_child", &[bullet.to_variant()]);
// ✅ 加到别的节点上不受影响
other_node.add_child(&bullet);
```

## 工作流

```sh
# 1. 新建/刷新带 Rust 工程的项目（必须用带 Rust 模块的二进制，文件名带 .rust 后缀）
./bin/godot.macos.editor.arm64.rust --headless --editor \
    --rust-init-project /path/to/MyGame --quit

# 2. 生成/刷新本引擎的绑定 JSON（首次构建前需要）
./bin/godot.macos.editor.arm64.rust --headless --editor \
    --path /path/to/MyGame --rust-regenerate-bindings --quit

# 3. 构建（在项目目录里跑 cargo，别处用 --manifest-path 会失败）
cd /path/to/MyGame && cargo build

# 4. 走引擎构建（顺带刷新描述符缓存，编辑器/CI 都用这条）
./bin/godot.macos.editor.arm64.rust --headless --path /path/to/MyGame --build-solutions --quit

# 5. 运行验证
./bin/godot.macos.editor.arm64.rust --headless --path /path/to/MyGame --quit-after 120
```

编辑器里 **F5 会先 `cargo build` 再启动游戏**，构建失败不会启动。改任何 Rust 代码都要重新构建，
构建产物是 `.godot/rust/target/debug/lib<crate>.dylib`。

**验证脚本是否真的生效**：启动日志里应有
`godot-script: registered 'Player' (base Node2D) for res://src/player.rs`；
没有这行说明 `register_script!` / `mod` / `register()` 三处缺了。

## 参考文件

- [references/script-api.md](references/script-api.md) — 宏与属性的完整参考：导出提示表、
  支持的类型、生命周期、`#[func]`/`#[opt]`/`#[signal]`、tool 脚本、手写 `impl RustScript`、命名规则。
- [references/engine-recipes.md](references/engine-recipes.md) — 已编译验证的引擎操作片段：
  输入、取节点、实例化场景、信号连接、计时器、单例、随机数、调用其他节点。
- [references/workflow.md](references/workflow.md) — 脚手架/构建/热重载/导出/调试/项目设置，
  以及编译错误与运行时错误对照表。
- [templates/script.rs](templates/script.rs) — 可直接复制的脚本骨架。

## 交付前自检

1. `cargo build` 通过（或 `--build-solutions` 无错误）。
2. `register_script!` + `mod` + `register()` 三处齐全。
3. headless 跑一遍，确认 `godot-script: registered ...` 与脚本自己的 `godot_print!` 输出。
4. 需要暴露给 GDScript 的能力都带 `#[func]`；信号用 `#[signal]` 声明后再 `emit_signal`。
5. 没有在脚本回调里直接 `add_child`/`remove_child` 自己的子节点。
