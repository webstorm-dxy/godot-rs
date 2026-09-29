# 03 · 写第一个可挂载的 Rust 脚本

目标：写一个 `Player` 脚本，挂到任意 `Node2D` 节点上，在检视面板里能改 `speed`，
运行时能看到 `ready` / `process` 回调。

## 1. 最小脚本

`src/player.rs`：

```rust
use godot::prelude::*;
use godot_script::prelude::*;

pub struct Player {
    owner: Gd<Node2D>,
    speed: f32,
}

impl RustScript for Player {
    /// 这个脚本能挂到哪些节点上：Node2D 及其子类都可以。
    type Base = Node2D;

    /// 编辑器里显示的类名。
    const CLASS_NAME: &'static str = "Player";
    /// 基类的引擎名字，必须和上面的 type Base 一致。
    const BASE_NAME: &'static str = "Node2D";

    /// 每个挂了该脚本的节点都会调用一次，用来初始化状态。
    fn new(owner: Gd<Self::Base>) -> Self {
        Self { owner, speed: 100.0 }
    }

    fn ready(&mut self) {
        godot_print!("Player ready, speed = {}", self.speed);
    }

    fn process(&mut self, delta: f64) {
        let distance = self.speed as f64 * delta;
        let _ = (&self.owner, distance);
    }
}

godot_script::register_script!(Player);
```

最后一行宏做两件事：生成 `pub fn register()`（给 crate 根调用），并把这个类型注册到
引擎的脚本注册表（同时记住它来自哪个文件）。

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

属性 = 面板可见 + 存进场景文件。声明属性和读写都显式写出来：

```rust
use godot_script::prelude::*;

impl RustScript for Player {
    // ...
    fn properties() -> Vec<PropertyInfo> {
        vec![
            PropertyInfo::new_export::<f32>("speed"),
            PropertyInfo::new_export::<GString>("display_name"),
        ]
    }

    fn get_property(&self, name: &str) -> Option<Variant> {
        match name {
            "speed" => Some(self.speed.to_variant()),
            "display_name" => Some(self.display_name.to_variant()),
            _ => None,
        }
    }

    fn set_property(&mut self, name: &str, value: &Variant) -> bool {
        match name {
            "speed" => {
                self.speed = f32::from_variant(value);
                true          // true = 我处理了这个属性
            }
            "display_name" => {
                self.display_name = GString::from_variant(value);
                true
            }
            _ => false,
        }
    }
}
```

`PropertyInfo::new_export::<T>()` 用的就是 gdext 的导出规则，所以 `f32`、`bool`、
`GString`、`Vector2`、`Gd<Node>` 等常见类型都可以直接用；复杂类型可以用
`PropertyInfo { ... }` 手工构造（提示、范围等，和 GDScript 的 `@export_range` 类似）。

场景保存后，值会在节点实例化时通过 `set_property` 回写到你的结构体里。

## 4. 生命周期回调一览

| 回调 | 触发时机 |
| --- | --- |
| `new(owner)` | 节点创建脚本实例时（构造状态） |
| `ready()` | 节点及其子节点进入场景树后 |
| `process(delta)` | 每个渲染帧 |
| `physics_process(delta)` | 每个物理帧 |
| `enter_tree()` | 进入场景树 |
| `exit_tree()` | 离开场景树 |

它们和 GDScript 的 `_ready` / `_process` / `_physics_process` / `_enter_tree` /
`_exit_tree` 一一对应；引擎调用 `has_method` + `call` 时分发到这些方法。

## 5. 给 GDScript / 编辑器暴露方法

`methods()` 声明有哪些方法，`call_method()` 真正执行。声明之后，GDScript 里就能写
`$Player.jump(5.0)`，编辑器的 `has_method()` / `get_method_list()` / 自动补全也会看到它们。

```rust
impl RustScript for Player {
    // ...
    fn methods() -> Vec<ScriptMethod> {
        vec![
            ScriptMethod::new("jump")
                .arg("height", VariantType::FLOAT)
                .returns(VariantType::FLOAT),
            ScriptMethod::new("reset"), // 无参数、无返回值
        ]
    }

    fn call_method(&mut self, name: &str, args: &[&Variant]) -> Result<Variant, CallErrorType> {
        match name {
            "jump" => {
                let height = args.first().map_or(2.0, |value| f32::from_variant(value));
                Ok((self.speed + height).to_variant())
            }
            "reset" => {
                self.speed = 100.0;
                Ok(Variant::nil())
            }
            // 不是自己的方法就交还给引擎，让它照常报“方法不存在”。
            _ => Err(CallErrorType::InvalidMethod),
        }
    }
}
```

- 参数按声明顺序从 `args` 里取：`args.first()`、`args.get(1)`；
- 有返回值用 `Ok(value.to_variant())`，没有返回值用 `Ok(Variant::nil())`；
- `methods()` 和 `call_method()` 的名字必须对得上，漏一个就会出现“明明写了却调不到”；
- `.as_const()` 表示该方法可以在只读实例上调用（和 GDScript 的 `const` 方法一致）。

## 6. 声明信号

```rust
impl RustScript for Player {
    // ...
    fn signals() -> Vec<ScriptSignal> {
        vec![ScriptSignal::new("jumped").arg("height", VariantType::FLOAT)]
    }

    fn call_method(&mut self, name: &str, args: &[&Variant]) -> Result<Variant, CallErrorType> {
        match name {
            "do_jump" => {
                let height = 5.0;
                // 参数列表和 GDScript 的 emit_signal 一样。
                self.owner.clone().emit_signal("jumped", &[height.to_variant()]);
                Ok(Variant::nil())
            }
            _ => Err(CallErrorType::InvalidMethod),
        }
    }
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
## 7. 拿到节点自己

`new(owner)` 里的 `owner` 就是这个脚本挂着的节点，把它存下来即可：

```rust
fn process(&mut self, delta: f64) {
    let mut node = self.owner.clone();
    let pos = node.get_position();
    node.set_position(pos + Vector2::new(self.speed * delta as f32, 0.0));
}
```

`Gd<Node2D>::clone()` 只是增加引用（不会复制节点），放心用。

## 8. 工具脚本（编辑器里也运行）

```rust
impl RustScript for Player {
    const IS_TOOL: bool = true;   // 编辑器里也执行 ready/process
    // ...
}
```

> 当前版本工具脚本的实例化路径已打通，但编辑器内的属性刷新/撤销集成还在路线图上，
> 见 [06-architecture.md](06-architecture.md)。

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

## 10. 一个脚本一个类

- 一个 `.rs` 文件里放**一个** `register_script!`；
- 一个文件里多个类时只有被注册的那个能挂载；
- 辅助模块（工具函数等）放在同目录，不会被注册，但会自动加入 `mod` 列表。
