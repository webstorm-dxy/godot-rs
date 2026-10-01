# 引擎操作片段（均已编译并运行验证）

所有片段都在 `#[godot_script_api] impl Xxx { ... }` 里使用，文件头部通常需要：

```rust
use godot::classes::{CharacterBody2D, Input, PackedScene, SceneTreeTimer, Timer};
use godot::global::randf_range;
use godot::prelude::*;
use godot_script::prelude::*;
```

## 取节点

`Gd<T>` 的方法多数要 `&mut self`，所以先 `clone()`（只是引用计数，不复制节点）：

```rust
// 有类型检查；不存在时返回 None（get_node_as 不存在会 panic）
if let Some(mut timer) = self.owner.clone().try_get_node_as::<Timer>("Timer") {
    timer.set_wait_time(0.5);
}
```

## 读写属性

```rust
let mut body = self.owner.clone();
let position = body.get_position();
body.set_global_position(position + Vector2::new(1.0, 0.0));
```

## 实例化场景并加为子节点

在脚本回调里给自己节点加子节点会 panic（见下），必须 `call_deferred`：

```rust
let scene: Gd<PackedScene> = load("res://bullet.tscn");   // 加载失败会 panic；可用 try_load
if let Some(mut bullet) = scene.instantiate() {
    bullet.set_name("Bullet");
    // ❌ self.owner.clone().add_child(&bullet);            // 触发重入 → panic
    self.owner.clone().call_deferred("add_child", &[bullet.to_variant()]);  // ✅
}
```

加在**别的**节点上是安全的：

```rust
if let Some(mut other) = self.owner.clone().try_get_node_as::<Node2D>("/root/Main/Other") {
    if let Some(mut bullet) = scene.instantiate() {
        other.add_child(&bullet);        // ✅
    }
}
```

`PackedScene` 还有类型化版本：`try_instantiate_as::<Node2D>()`（返回 `Option`）与
`instantiate_as::<Node2D>()`（失败 panic）。

报错原文：

```text
ScriptInstance borrow failed, already bound; T = ...::Player.
  Details: cannot borrow while accessible mutable borrow exists.
```

## 信号

**把子节点的信号接到自己的 `#[func]` 方法**（`#[func]` 是必须的，否则引擎找不到该方法）：

```rust
if let Some(mut timer) = self.owner.clone().try_get_node_as::<Timer>("Timer") {
    let callable = Callable::from_object_method(&self.owner, "on_timeout");
    let _error = timer.connect("timeout", &callable);
    timer.start();                       // 别忘了启动；wait_time 必须 > 0
}

#[func]
fn on_timeout(&mut self) {
    godot_print!("timer fired");
}
```

**无状态闭包**可以用类型化信号：

```rust
timer.signals().timeout().connect(|| godot_print!("fired"));
```

（类型化信号的 `connect_self`/`connect_other` 需要 `Base<T>` 字段，那是
`#[derive(GodotClass)]` 节点类型的用法，脚本类用不了；脚本请用上面的 `Callable` 写法。）

**发射自己的信号**：

```rust
self.owner.clone().emit_signal("jumped", &[5.0.to_variant()]);
```

## 计时器与 SceneTree

```rust
// get_tree() 在 gdext 里是**不返回 Option** 的（节点不在树里会报错）
let mut tree: Gd<SceneTree> = self.owner.clone().get_tree();
let _one_shot: Gd<SceneTreeTimer> = tree.create_timer(1.0);

// 允许不在树里时用：
if let Some(_tree) = self.owner.clone().get_tree_or_null() { }
```

## 单例与全局函数

prelude 重导出了 `Singleton` trait，所以直接 `singleton()`：

```rust
if Input::singleton().is_action_just_pressed("ui_accept") {
    godot_print!("accepted");
}
```

`@GlobalScope` 里的函数在 `godot::global`：

```rust
use std::f64::consts::TAU;              // 引擎的 TAU 不在 gdext 里
let angle = randf_range(0.0, TAU);      // godot::global::randf_range
```

## 调用其他节点的方法

```rust
if let Some(mut other) = self.owner.clone().try_get_node_as::<Node2D>("/root/Main/Other") {
    if other.has_method("greet") {
        let reply = other.call("greet", &[GString::from("hi").to_variant()]);
        godot_print!("reply {}", reply);
    }
}
```

方法不存在时 gdext 会报错（strict safeguards 下是 panic），所以先 `has_method`。

## 角色移动（CharacterBody2D）

```rust
fn physics_process(&mut self, delta: f64) {
    let mut body = self.owner.clone();
    let mut velocity = body.get_velocity();
    velocity.y += self.gravity * delta as f32;
    body.set_velocity(velocity);
    let _ = body.move_and_slide();
}
```

## 其它

```rust
self.owner.clone().queue_free();                                 // 安全，引擎自己延后处理
godot_print!("value = {}", 1);                                   // godot_print!/godot_warn!/godot_error!
let callable = Callable::from_object_method(&self.owner, "on_timeout");
```
