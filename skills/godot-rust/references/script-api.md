# 脚本 API 参考

以下内容对应 `modules/rust/support/godot-script`（trait `RustScript`）与
`modules/rust/support/godot-script-derive`（两个宏），示例均已在本仓库编译验证。

## 1. `#[derive(RustScript)]`：结构体

只支持**具名字段的结构体**。

```rust
#[derive(RustScript)]
#[script(base = CharacterBody2D, tool, name = "CustomName")]
pub struct Player { /* ... */ }
```

| `#[script(...)]` 选项 | 必填 | 说明 |
| --- | --- | --- |
| `base = <引擎类>` | ✅ | 脚本可挂载的基类；缺省会编译失败：`add #[script(base = YourBaseNode)] to say which engine class this script extends` |
| `tool` | | 编辑器里也运行（等价 GDScript 的 `@tool`）；不带则只在游戏里运行 |
| `name = "CustomName"` | | 类名，默认取结构体名 |

字段：

- **`owner`**：字段名为 `owner` 时，自动注入挂载的节点 `Gd<Base>`（只是引用，`clone()` 不复制节点）。
  字段不叫 `owner` 时构造参数会被忽略（`let _ = owner;`），需要在 `ready()` 里自己保存。
- 其余字段若不带 `#[export]`，就是普通 Rust 字段，初始值 `Default::default()`。
- `#[export]` 字段进入检视面板并随场景保存，GDScript 可用 `node.get("speed")` / `node.set("speed", 7.0)`。

## 2. `#[export(...)]` 提示

一个属性**只能有一个提示**（第二个会报 `a property can only have one hint`）；
`default` 可与提示组合；`storage` 不能与提示组合。

| 写法 | 面板效果 |
| --- | --- |
| `#[export]` | 普通可编辑字段 |
| `#[export(default = 42.0)]` | 初始值（同时是检视面板"还原"箭头的值） |
| `#[export(range = (0.0, 100.0))]` / `range = (0.0, 100.0, 0.5)` | 滑条（可带步长） |
| `#[export(enum = ["Idle", "Run"])]` | 下拉框（存的是下标，配整数类型） |
| `#[export(flags = ["Fire", "Ice"])]` | 位标志多选 |
| `#[export(file = "*.png")]` / `#[export(global_file = "*.txt")]` | 文件选择（项目路径 / 绝对路径） |
| `#[export(dir)]` | 目录选择 |
| `#[export(node = "Node2D")]` | 只允许该类型的节点路径（配 `NodePath`） |
| `#[export(placeholder = "type here")]` | 文本框占位符（配 `GString`） |
| `#[export(multiline)]` | 多行文本框（配 `GString`） |
| `#[export(color_no_alpha)]` | 不带透明度的颜色（配 `Color`） |
| `#[export(exp_easing)]` | 缓动曲线（配数值） |
| `#[export(storage)]` | 保存进场景但**不在面板显示** |

已编译验证的完整示例：

```rust
use godot::classes::CharacterBody2D;
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = CharacterBody2D)]
pub struct Player {
    owner: Gd<CharacterBody2D>,

    #[export] speed: f32,
    #[export(default = 120.0)] jump_velocity: f32,
    #[export(default = 1.0, range = (0.0, 100.0, 0.5))] gravity: f32,
    #[export(default = 0, enum = ["Idle", "Run"])] mode: i64,
    #[export(default = 0, flags = ["Fire", "Ice"])] elements: i64,
    #[export(file = "*.png")] icon_path: GString,
    #[export(global_file = "*.txt")] notes_path: GString,
    #[export(dir)] folder: GString,
    #[export(node = "Node2D")] target: NodePath,
    #[export(default = GString::from("hero"), placeholder = "type here")] nick: GString,
    #[export(multiline)] bio: GString,
    #[export(color_no_alpha)] tint: Color,
    #[export(exp_easing)] easing: f32,
    #[export(default = 3, storage)] secret: i64,

    plain: i64,                       // 不导出
}
```

### 类型规则

- `default = ...` 表达式的类型**必须是字段的 Rust 类型**：
  `GString` 字段写 `default = GString::from("hero")`，写 `default = ""` 会报
  `mismatched types: expected GString, found &str`。
- 导出的节点引用必须是 `Option<Gd<T>>`：
  `#[export] node_ref: Option<Gd<Node2D>>` ✅；`#[export] node_ref: Gd<Node2D>` ❌
  （`type cannot be used as a property`，且 `Gd<T>` 没有 `Default`）。
- 其它可用类型（与 gdext 一致）：`f32`/`f64`/`i64`/`bool`、`GString`、`StringName`、
  `Vector2`/`Vector3`、`Color`、`NodePath`、`Array<T>` 等。
- 想要属性名与字段名不同、或动态生成属性表，只能手写 `impl RustScript`（见第 6 节）。

## 3. `#[godot_script_api]`：impl 块

### 生命周期（按名字识别，**不加** `#[func]`）

| 方法（签名必须一致） | 触发时机 |
| --- | --- |
| `fn ready(&mut self)` | 节点及子节点进入场景树之后 |
| `fn process(&mut self, delta: f64)` | 每个渲染帧 |
| `fn physics_process(&mut self, delta: f64)` | 每个物理帧 |
| `fn enter_tree(&mut self)` | 进入场景树 |
| `fn exit_tree(&mut self)` | 离开场景树 |

`delta` 单位是秒，类型 `f64`。

### `#[func]` 方法

```rust
#[godot_script_api]
impl Player {
    #[func]
    fn jump(&mut self, height: f32) -> f32 { self.speed + height }   // 有返回值

    #[func]
    fn reset(&mut self) { self.speed = 100.0; }                      // 返回 null

    #[func]
    fn describe(&mut self) -> GString { GString::from("player") }

    #[func]
    fn boost(
        &mut self,
        amount: f32,
        #[opt(default = 1.0)] extra: f32,      // 可选参数必须排在最后
        #[opt(default = 2)] times: i64,
    ) -> f32 {
        amount + extra * times as f32
    }
}
```

GDScript 侧：`player.has_method("jump")` → `true`；`player.call("jump", 5.0)`；
参数会自动转换，个数或类型不对时引擎报调用错误。可选参数会随描述符注册，
编辑器和 `get_method_list()` 都能看到默认值。

### `#[signal]`

```rust
    #[signal]
    fn jumped(height: f32) {}      // 花括号里不写内容，方法体不会被编译
```

发射用引擎接口，参数列表与 GDScript 的 `emit_signal` 一致：

```rust
self.owner.clone().emit_signal("jumped", &[height.to_variant()]);
```

GDScript 里 `player.jumped.connect(...)`、`player.has_signal("jumped")`、
编辑器 Node → Signals 面板都能用。没有人连接的信号发射时不会报错。

## 4. 注册链（缺一不可）

```rust
// src/player.rs 末尾
godot_script::register_script!(Player);      // 生成 pub fn register()
```

```rust
// src/lib.rs
mod player;                                   // ①
pub fn register_scripts() {
    player::register();                       // ②
}
```

`register_scripts()` 由脚手架的 `#[gdextension] impl ExtensionLibrary::on_stage_init`
在 `InitStage::Scene` 调用。编辑器创建/保存脚本、打开项目时会扫描 `src/*.rs` 自动补齐 ① ②
（只对含 `register_script!` 的文件补 ②）。

## 5. 命名与文件规则

| 项 | 规则 | 例 |
| --- | --- | --- |
| 文件名 | 合法 Rust 模块名：字母/数字/下划线，不以数字开头，不能是关键字；推荐小写下划线 | `player.rs`、`my_enemy.rs` ✅；`my-enemy.rs`、`2d_player.rs` ❌ |
| 结构体名 | 合法 Rust 类型名；编辑器按 UpperCamelCase 从文件名生成 | `sprite_2d.rs` → `Sprite2D`、`PLAYER.rs` → `Player` |
| `CLASS_NAME` / `name` | 全局唯一（编辑器显示、场景引用都用它） | `"MyEnemy"` |

编辑器侧还可以直接调用转换函数：

```gdscript
RustLanguage.to_rust_type_name("sprite_2d")       # "Sprite2D"
RustLanguage.is_valid_module_name("my-script")    # false
RustLanguage.to_rust_class_name("GPUParticles2D") # "GpuParticles2D"
RustLanguage.make_script_source("Player", "Node2D")
```

## 6. 手写 `impl RustScript`（进阶，与派生宏互斥）

```rust
use godot::classes::Node2D;
use godot::prelude::*;
use godot_script::prelude::*;

pub struct Manual { owner: Gd<Node2D>, speed: f32 }

impl RustScript for Manual {
    type Base = Node2D;
    const CLASS_NAME: &'static str = "Manual";
    // BASE_NAME 留空即可：会自动取 type Base 的引擎类名（GpuParticles2D 这类拼写差异由它兜底）

    fn new(owner: Gd<Self::Base>) -> Self { Self { owner, speed: 100.0 } }

    fn properties() -> Vec<PropertyInfo> { vec![PropertyInfo::new_export::<f32>("speed")] }
    fn get_property(&self, name: &str) -> Option<Variant> {
        match name { "speed" => Some(self.speed.to_variant()), _ => None }
    }
    fn set_property(&mut self, name: &str, value: &Variant) -> bool {
        match name { "speed" => { self.speed = f32::from_variant(value); true } _ => false }
    }

    fn methods() -> Vec<ScriptMethod> {
        vec![ScriptMethod::new("jump").arg("height", VariantType::FLOAT).returns(VariantType::FLOAT)]
    }
    fn call_method(&mut self, name: &str, args: &[&Variant]) -> Result<Variant, CallErrorType> {
        match name {
            "jump" => Ok((self.speed + args.first().map_or(2.0, |v| f32::from_variant(v))).to_variant()),
            _ => Err(CallErrorType::InvalidMethod),
        }
    }

    fn signals() -> Vec<ScriptSignal> {
        vec![ScriptSignal::new("jumped").arg("height", VariantType::FLOAT)]
    }

    fn ready(&mut self) { godot_print!("Manual ready, speed = {}", self.speed); }
}

godot_script::register_script!(Manual);
```

同时写 `#[derive(RustScript)]` 和手写 impl 会报
`conflicting implementations of trait godot_script::RustScript for type ...`。

可选覆盖：`fn property_default(name: &str) -> Option<Variant>`（检视面板还原值）、
`fn process/physics_process/enter_tree/exit_tree`、`const IS_TOOL: bool`。
辅助函数：`call_argument(args, i)`（缺参数时报 `TooFewArguments`）、
`argument_value(value)`（类型不匹配时报 `InvalidArgument`）、`ScriptMethod::arg_of::<T>`。

## 7. 描述符、占位实例与生效时机

- 检视面板的属性/方法/信号来自 Rust 侧注册的**描述符**，缓存于
  `.godot/rust/script_cache.json`，而不是解析源码。
- **首次构建成功之前**，挂上的脚本是占位实例，类名会报 `ERROR: Cannot get class 'Player'`；
  构建成功后占位实例会升级成真实例并获得一次 `ready()` 调用。
- 属性右端的"还原"箭头取 `#[export(default = ...)]`（未写则是 `Default::default()`），
  需要脚本库已加载；对应引擎 API 是 `property_can_revert()` / `property_get_revert()`。
- 导出属性会被复制迁移到新库的实例（热重载），但**私有字段与 static 不会**，
  `ready()` 也不会重跑（占位实例升级除外）。

## 8. 当前不支持

- RPC（`@rpc` 风格的多人同步）。
- `#[derive(GodotClass)]` 节点类型在热重载后需要重启编辑器（一个类在一个进程里不能注册两次）。
- Android / iOS / Web 导出；跨平台导出需手工交叉编译。
