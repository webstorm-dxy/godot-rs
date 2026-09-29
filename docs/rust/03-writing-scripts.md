# 03 · 写第一个可挂载的 Rust 脚本

目标：写一个 `Player` 脚本，挂到任意 `Node2D` 节点上，在检视面板里能改 `speed`，
运行时能收到 `ready` / `process` 回调，还能被 GDScript 调用、能发信号。

脚本用两个宏写成：`#[derive(RustScript)]` 读**结构体**（字段、检视面板设置），
`#[godot_script_api]` 读 **impl 块**（生命周期、方法、信号）。

## 1. 最小脚本

`src/player.rs`：

```rust
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D)]        // 能挂到 Node2D 及其子类上
pub struct Player {
    owner: Gd<Node2D>,          // 名字叫 owner 的字段自动拿到挂载的节点
    #[export]                   // 显示在检视面板，并保存进场景
    speed: f32,
}

#[godot_script_api]
impl Player {
    fn ready(&mut self) {
        godot_print!("Player ready, speed = {}", self.speed);
    }

    fn process(&mut self, delta: f64) {
        let distance = self.speed as f64 * delta;
        let _ = distance;
    }
}

godot_script::register_script!(Player);
```

规则：

- `#[script(base = 引擎类名)]` 必填；类名默认就是结构体名（可用 `#[script(name = "Other")]` 改）；
- 没有 `#[export]` 的字段是普通 Rust 字段，初始值为 `Default::default()`；
- 最后一行宏生成 `pub fn register()`（给 crate 根调用）并把类型注册到引擎。

## 2. 让 crate 根知道它

`src/lib.rs`：

```rust
mod player;                 // ① 声明模块

pub fn register_scripts() {
    player::register();     // ② 注册脚本
}
```

在编辑器里点 **Attach Script** 新建的脚本，这两行会自动写进去；手写文件时需要自己加。

## 3. 导出属性（在检视面板里编辑）

```rust
#[derive(RustScript)]
#[script(base = Node2D)]
pub struct Player {
    owner: Gd<Node2D>,

    #[export]                              // 面板里可编辑、存进场景
    speed: f32,

    #[export(default = 10.0)]              // 初始值不是 Default
    height: f32,

    #[export(default = 1.0, range = (0.0, 100.0))]   // 面板里是滑条
    gravity: f32,

    display_name: GString,                 // 不导出：只是内部字段
}
```

支持的类型就是 gdext 的导出类型：`f32`、`f64`、`i64`、`bool`、`GString`、
`Vector2`、`Gd<某个节点>` 等。

`#[export(...)]` 里可以写一个**提示**，对应 GDScript 的 `@export_*`：

| 写法 | 面板效果 |
| --- | --- |
| `range = (0.0, 100.0)` / `range = (0.0, 100.0, 0.5)` | 滑条（可选步长） |
| `enum = ["Low", "High"]` | 下拉框（值是下标） |
| `flags = ["A", "B"]` | 位标志多选 |
| `file = "*.png"` / `global_file = "*.txt"` | 文件选择（过滤/绝对路径） |
| `dir` | 目录选择 |
| `multiline` | 多行文本框 |
| `placeholder = "type here"` | 文本框占位符 |
| `node = "Node2D"` | 只允许该类型的节点路径 |
| `color_no_alpha` | 不带透明度的颜色 |
| `exp_easing` | 缓动曲线 |
| `storage` | 保存进场景但面板里隐藏 |

一个属性只能有一个提示；写法可以组合 `default`，例如：

```rust
#[export(default = 1, enum = ["One", "Two"])]
mode: i64,
```

更复杂的类型（自定义 `PropertyInfo`）可以手写 impl，见第 10 节。

场景保存时属性值会随节点一起存下来，重新打开场景时自动写回字段。

检出面板里属性右边的“还原”箭头也能用：它的值来自 `#[export(default = ...)]`
（没写就是 `Default::default()`）。注意这需要脚本库已经加载（占位实例不提供还原值）。

## 4. 生命周期回调

在 `#[godot_script_api]` 的 impl 块里写下面这些名字的方法即可，签名必须一致：

| 方法 | 触发时机 |
| --- | --- |
| `fn ready(&mut self)` | 节点及其子节点进入场景树之后 |
| `fn process(&mut self, delta: f64)` | 每个渲染帧 |
| `fn physics_process(&mut self, delta: f64)` | 每个物理帧 |
| `fn enter_tree(&mut self)` | 进入场景树 |
| `fn exit_tree(&mut self)` | 离开场景树 |

它们和 GDScript 的 `_ready` / `_process` / `_physics_process` / `_enter_tree` /
`_exit_tree` 一一对应；`delta` 的类型是 `f64`（秒）。

## 5. 给 GDScript / 编辑器暴露方法

给方法加 `#[func]`，GDScript 里就能 `player.call("jump", 5.0)`，编辑器的
`has_method()` / `get_method_list()` / 自动补全也会看到它：

```rust
#[godot_script_api]
impl Player {
    #[func]
    fn jump(&mut self, height: f32) -> f32 {
        self.speed + height
    }

    #[func]
    fn reset(&mut self) {          // 没有返回值：GDScript 里返回 null
        self.speed = 100.0;
    }

    #[func]
    fn describe(&mut self) -> GString {
        GString::from("player")
    }
}
```

GDScript 侧：

```gdscript
print(player.has_method("jump"))          # true
print(player.call("jump", 5.0))           # 105.0
print(player.get_method_argument_count("jump"))   # 1
```

- 参数类型会自动转换；参数个数不够或类型不对，引擎会给出调用错误；
- 参数名会出现在编辑器的提示里。

**默认参数**：用 `#[opt(default = ...)]` 标记，必须放在参数表最后，GDScript 就可以不传：

```rust
#[godot_script_api]
impl Player {
    #[func]
    fn boost(
        &mut self,
        amount: f32,
        #[opt(default = 1.0)] extra: f32,      // 可以不传
        #[opt(default = 2)] times: i64,        // 每个可选参数都要写默认值
    ) -> f32 {
        amount + extra * times as f32
    }
}
```

```gdscript
player.call("boost", 1.0)          # extra = 1.0, times = 2
player.call("boost", 1.0, 5.0)     # times = 2
player.call("boost", 1.0, 5.0, 3)  # 全部传入
```

默认值会随描述符一起注册，编辑器和 `get_method_list()` 里都能看到。

## 6. 声明信号

```rust
#[godot_script_api]
impl Player {
    #[func]
    fn do_jump(&mut self) {
        let height = 5.0;
        // 参数列表和 GDScript 的 emit_signal 一样
        self.owner.clone().emit_signal("jumped", &[height.to_variant()]);
    }

    #[signal]
    fn jumped(height: f32) {}      // 花括号里不写内容
}
```

GDScript 侧和普通信号完全一样：

```gdscript
player.jumped.connect(_on_player_jumped)
# 等价写法：player.connect("jumped", Callable(self, "_on_player_jumped"))
print(player.has_signal("jumped"))   # true
```

编辑器里右侧的 **Node → Signals** 面板同样会列出它并可以连接。和 GDScript 一致：
**没有人连接的信号，发射时不会报错，也不会有任何效果**。

## 7. 工具脚本（编辑器里也运行）

```rust
#[derive(RustScript)]
#[script(base = Node2D, tool)]     // tool = 编辑器里也执行
pub struct GridPainter {
    owner: Gd<Node2D>,
}
```

规则和 GDScript 完全一致：**不带 `tool` 的脚本在编辑器里不运行**（编辑器只用一个占位
实例显示属性），按 F5 跑游戏时才执行；带 `tool` 的脚本两边都会执行，适合做编辑器辅助
工具（自动摆放、批量检查等）。

注意：编辑器里的实例和运行游戏时的实例是两套，字段状态不共享。

## 8. 拿到节点自己

`owner` 字段就是挂着的节点（`Gd<Node2D>`），它只是引用，`clone()` 不会复制节点：

```rust
fn process(&mut self, delta: f64) {
    let mut node = self.owner.clone();
    let pos = node.get_position();
    node.set_position(pos + Vector2::new(self.speed * delta as f32, 0.0));
}
```

也可以叫别的名字：字段不叫 `owner` 时，`new(owner)` 里的节点会被忽略（会有编译提示），
需要的话在 `ready()` 里自己 `self.owner = ...` 保存。

## 9. 命名规则（很重要）

| 项 | 规则 | 例子 |
| --- | --- | --- |
| 文件名 | 必须是合法的 **Rust 模块名**：字母、数字、下划线，不以数字开头，不能是关键字。推荐小写加下划线（Rust 惯例） | `player.rs`、`my_enemy.rs` ✅ / `my-enemy.rs`、`2d_player.rs` ❌ |
| 结构体名 | 合法的 Rust 类型名；编辑器创建脚本时按 **Rust 标准 UpperCamelCase** 从文件名生成 | `player.rs` → `struct Player`、`my_enemy.rs` → `struct MyEnemy`、`sprite_2d.rs` → `struct Sprite2D`、`PLAYER.rs` → `struct Player` |
| `CLASS_NAME` | 全局唯一（编辑器里显示、场景引用都用它） | `"MyEnemy"` |

编辑器创建脚本时如果文件名不合法，会直接给出提示、不生成文件；手动写文件时请自己遵守。

命名转换规则也可以在编辑器脚本/工具里直接调用（便于批量重命名等）：

```gdscript
RustLanguage.to_rust_type_name("sprite_2d")     # "Sprite2D"
RustLanguage.to_rust_type_name("my-script")     # "MyScript"
RustLanguage.is_valid_module_name("my-script")  # false
```

规则细节：按非字母数字切词、每词首字母大写（`sprite_2d` → `Sprite2D`）；全大写词按普通词处理
（`PLAYER` → `Player`）；以数字开头时加 `Rust` 前缀；`Self` 改写为 `SelfScript`。

`mod` 声明与注册调用不需要手写：保存脚本时、以及每次打开编辑器时，模块都会扫描 `src/*.rs`
并补齐（注册调用仅当文件里出现 `register_script!` 时才会加），所以旧项目也会自动修好。

## 10. 不用派生宏：手写 impl（进阶）

派生宏只是省样板。想要完全控制（属性名和字段名不同、动态生成方法表、特殊提示等），
可以直接实现 `RustScript`：

```rust
use godot::prelude::*;
use godot_script::prelude::*;

pub struct Player {
    owner: Gd<Node2D>,
    speed: f32,
}

impl RustScript for Player {
    type Base = Node2D;
    const CLASS_NAME: &'static str = "Player";
    const BASE_NAME: &'static str = "Node2D";

    fn new(owner: Gd<Self::Base>) -> Self {
        Self { owner, speed: 100.0 }
    }

    fn properties() -> Vec<PropertyInfo> {
        vec![PropertyInfo::new_export::<f32>("speed")]
    }

    fn get_property(&self, name: &str) -> Option<Variant> {
        match name {
            "speed" => Some(self.speed.to_variant()),
            _ => None,
        }
    }

    fn set_property(&mut self, name: &str, value: &Variant) -> bool {
        match name {
            "speed" => {
                self.speed = f32::from_variant(value);
                true
            }
            _ => false,
        }
    }

    fn methods() -> Vec<ScriptMethod> {
        vec![ScriptMethod::new("jump").arg("height", VariantType::FLOAT).returns(VariantType::FLOAT)]
    }

    fn call_method(&mut self, name: &str, args: &[&Variant]) -> Result<Variant, CallErrorType> {
        match name {
            "jump" => {
                let height = args.first().map_or(2.0, |value| f32::from_variant(value));
                Ok((self.speed + height).to_variant())
            }
            _ => Err(CallErrorType::InvalidMethod),
        }
    }

    fn signals() -> Vec<ScriptSignal> {
        vec![ScriptSignal::new("jumped").arg("height", VariantType::FLOAT)]
    }

    fn ready(&mut self) {
        godot_print!("Player ready, speed = {}", self.speed);
    }
}

godot_script::register_script!(Player);
```

注意：**派生宏和手写 impl 不能混用**（派生宏已经生成了整个 `impl RustScript`）。

## 11. 一个脚本一个类

- 一个 `.rs` 文件里放**一个** `register_script!`；
- 一个文件里多个类时只有被注册的那个能挂载；
- 辅助模块（工具函数等）放在同目录，不会被注册，但会自动加入 `mod` 列表。
