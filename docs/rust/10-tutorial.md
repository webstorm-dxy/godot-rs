# 10 · 手把手教程：做一个“接星星”小游戏

这份教程从头做一个能玩的小游戏，每一章都有**完整代码**和**你应该看到的结果**。
不需要任何 Godot 或 Rust 经验，跟着敲一遍，你就掌握了这个模块 90% 的用法。

**你会学到：**

| 章 | 内容 | 对应能力 |
| --- | --- | --- |
| 1 | 建项目、跑起来 | 项目脚手架 |
| 2 | 第一个 Rust 脚本 | `#[derive(RustScript)]`、生命周期 |
| 3 | 让玩家动起来 | 输入、导出属性、检视面板调参 |
| 4 | 给 GDScript 用的方法 | `#[func]`、默认参数 |
| 5 | 信号 | `#[signal]`、Rust 与 GDScript 通信 |
| 6 | 用 Rust 写节点类型 | `#[derive(GodotClass)]` |
| 7 | 把游戏接起来 | 场景、脚本互调 |
| 8 | 编辑器里的日常 | rust-analyzer、错误定位、热重载 |
| 9 | 断点调试 | lldb-dap、单步、变量 |
| 10 | 导出发布 | 打包成别人能玩的游戏 |

**时间**：跟着做约 1 小时。

---

## 0. 开始之前

确认两件事：

```sh
# ① 引擎编译好了（本仓库根目录）
ls bin/godot.macos.editor.arm64.rust

# ② cargo 可用
cargo --version
```

引擎的编译方法见 [01-installation.md](01-installation.md)。下面所有命令里的 `$GODOT`
都指那个引擎二进制的路径：

```sh
export GODOT=/path/to/godot-rs/bin/godot.macos.editor.arm64.rust
```

---

## 1. 第 1 步：建项目，先跑起来

### 1.1 用编辑器新建（推荐）

打开编辑器，在项目管理器里 **新建项目**，勾选 **Rust**（或建好后在 *项目设置* 里打开
`rust/enabled`）。模块会自动生成：

```
MyGame/
├── project.godot          # 里含 rust/enabled=true、run/main_scene
├── Cargo.toml             # 依赖已指向引擎里自带的 gdext，无需联网
├── src/
│   ├── lib.rs             # 扩展入口（注册所有脚本）
│   └── player.rs          # 示例脚本
└── .godot/rust/           # 模块自己的工作目录（构建产物、绑定、缓存）
    ├── bindings/          # 从本引擎导出的 API，gdext 用它生成绑定
    ├── target/            # cargo 的构建目录
    └── script_cache.json  # 未构建时编辑器用它显示属性
```

### 1.2 或者用命令行（CI / 脚本化）

```sh
"$GODOT" --headless --editor --rust-init-project /tmp/MyGame --quit
```

### 1.3 先跑一次

按 **F5**（或运行栏的 ▶）。第一次会先 `cargo build`（几秒到几十秒），然后弹出游戏窗口。

**你应该看到**编辑器下方的 *Rust* 面板里出现 cargo 输出，最后是 `Finished \`dev\` profile`；
游戏窗口里是空的（还没有内容），控制台里有一行：

```
Player ready, speed = 0
```

> 为什么已经有输出了？因为脚手架生成的 `src/player.rs` 里已经有一个最小脚本，
> 它的 `ready()` 里打印了这行。第 2 步我们就从它开始改。

### 1.4 小练习

把 `src/player.rs` 里 `godot_print!` 的文字改掉，再按 F5，确认新文字出现。
（如果没变化，看第 8 章的热重载。）

---

## 2. 第 2 步：第一个 Rust 脚本

### 2.1 脚本长什么样

打开 `src/player.rs`，它大概是：

```rust
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D)]
pub struct Player {
    owner: Gd<Node2D>,
    #[export]
    speed: f32,
}

#[godot_script_api]
impl Player {
    fn ready(&mut self) {
        godot_print!("Player ready, speed = {}", self.speed);
    }
}

godot_script::register_script!(Player);
```

逐段理解：

| 片段 | 含义 |
| --- | --- |
| `#[derive(RustScript)]` | 把这个结构体变成一个**可挂到节点上的脚本** |
| `#[script(base = Node2D)]` | 只能挂到 `Node2D` 及其子类（`CharacterBody2D`、`Sprite2D`…）上 |
| `owner: Gd<Node2D>` | 固定名字 `owner`：脚本挂着的那个节点 |
| `#[export]` | 显示在检视面板里，并随场景保存 |
| `#[godot_script_api]` | 写生命周期 / 方法 / 信号的地方 |
| `fn ready(&mut self)` | 对应 GDScript 的 `_ready()` |
| `register_script!(Player)` | 文件末尾一行，把脚本注册给引擎 |

### 2.2 挂到节点上

1. 在场景里新建一个 **Node2D**，改名 `Player`；
2. 右键节点 → **Attach Script…**；
3. **Language** 选 `Rust`；类名填 `Player`，路径保持 `res://src/player.rs`；
4. 点 **Create**。

编辑器会：生成脚本 + 自动往 `src/lib.rs` 里补 `mod player;` 和 `player::register();`
+ 把脚本挂到节点上。此时是**占位实例**（还没编译），检视面板已经能看到 `Speed` 属性。

### 2.3 验证

按 F5。**你应该看到**：

```
Player ready, speed = 0
```

### 2.4 小练习

把 `speed` 的初值改成 `150.0`（写 `#[export(default = 150.0)]`），再跑一次，确认打印变了。

---

## 3. 第 3 步：让玩家动起来

### 3.1 目标

方向键控制 `Player` 移动，速度在检视面板里能实时调。

### 3.2 代码

把 `src/player.rs` 换成：

```rust
use godot::prelude::*;
use godot::classes::Input;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D)]
pub struct Player {
    owner: Gd<Node2D>,

    /// 每秒移动多少像素，检视面板里是 0~600 的滑条。
    #[export(default = 200.0, range = (0.0, 600.0))]
    speed: f32,

    /// 收集到的分数，随场景保存。
    #[export]
    score: i64,
}

#[godot_script_api]
impl Player {
    fn ready(&mut self) {
        godot_print!("Player ready, speed = {}", self.speed);
    }

    fn process(&mut self, delta: f64) {
        let input = Input::singleton();
        let mut direction = Vector2::ZERO;
        if input.is_action_pressed("ui_right") {
            direction.x += 1.0;
        }
        if input.is_action_pressed("ui_left") {
            direction.x -= 1.0;
        }
        if input.is_action_pressed("ui_down") {
            direction.y += 1.0;
        }
        if input.is_action_pressed("ui_up") {
            direction.y -= 1.0;
        }

        if direction == Vector2::ZERO {
            return;
        }
        let mut node = self.owner.clone();
        let position = node.get_position();
        node.set_position(position + direction.normalized() * self.speed * delta as f32);
    }
}

godot_script::register_script!(Player);
```

要点：

- `process(&mut self, delta: f64)` 就是每一帧调用一次（`delta` 是距上一帧的秒数，
  所以乘以 `delta` 之后速度与帧率无关）；
- `Input::singleton()` 拿引擎的输入单例，`ui_left/ui_right/ui_up/ui_down` 是 Godot
  内置的方向键动作，不用自己配；
- `self.owner.clone()` 拿到节点本身。`Gd<T>` 是引用计数句柄，`clone()` 不会复制节点；
- 想改节点属性必须 `let mut node = ...`（`&mut` 才允许写）。

### 3.3 验证

按 F5，用方向键移动（现在画面是空的，可以给 `Player` 加一个 `Sprite2D` 子节点随便指定
一张图，或者在 `ready()` 里临时 `godot_print!` 打印坐标）。
在检视面板里把 **Speed** 拖到 600，移动明显变快：**属性改了立刻生效，不需要重新编译**。

### 3.4 小练习

把 `range` 改成 `(0.0, 600.0, 50.0)`，面板里就会按 50 一档走。
---

## 4. 第 4 步：给 GDScript 用的方法

### 4.1 目标

GDScript（或编辑器）能调用 Rust 的方法：`player.add_score(2)`。

### 4.2 代码

在 `#[godot_script_api] impl Player { ... }` 里加：

```rust
    /// 加分并返回总分。GDScript 里：player.add_score(2)
    #[func]
    fn add_score(&mut self, points: i64) -> i64 {
        self.score += points;
        self.score
    }

    /// 带默认参数：GDScript 里 boost(10) / boost(10, 2.0)
    #[func]
    fn boost(
        &mut self,
        points: i64,
        #[opt(default = 1.0)] multiplier: f32,
    ) -> i64 {
        let gained = (points as f32 * multiplier).round() as i64;
        self.score += gained;
        self.score
    }
```

### 4.3 GDScript 侧

```gdscript
extends Node2D

@onready var player := $Player

func _ready() -> void:
	print(player.has_method("add_score"))     # true
	print(player.add_score(2))                # 2
	print(player.boost(10))                   # 12（默认倍率 1.0）
	print(player.boost(10, 2.5))              # 37
	print(player.get("score"))                # 37（导出属性能读）
	player.set("score", 0)                    # 也能写
```

### 4.4 规则

- 只有 `#[func]` 的方法才对外可见；普通方法（不加属性）只在 Rust 内部用；
- 参数个数不足 / 类型不对时引擎会报调用错误，和 GDScript 一致；
- `#[opt(default = ...)]` 必须放在参数表**最后**，而且每个可选参数都要写默认值。

### 4.5 小练习

加一个 `fn reset(&mut self)`，把 `score` 归零，在 GDScript 里调用它。

---

## 5. 第 5 步：信号（Rust → GDScript）

### 5.1 目标

加分时发出 `score_changed` 信号，GDScript 里连接它更新 UI。

### 5.2 代码

`impl Player` 里加两处：

```rust
    #[func]
    fn add_score(&mut self, points: i64) -> i64 {
        self.score += points;
        // 发信号：名字 + 参数列表（Variant）
        self.owner.clone().emit_signal("score_changed", &[self.score.to_variant()]);
        self.score
    }

    /// 只写声明，花括号里不写内容
    #[signal]
    fn score_changed(total: i64) {}
```

### 5.3 GDScript 侧

```gdscript
func _ready() -> void:
	player.score_changed.connect(_on_score_changed)
	player.add_score(3)

func _on_score_changed(total: int) -> void:
	print("score = ", total)      # score = 3
```

### 5.4 注意

- 和 GDScript 一样：**没有连接的信号发射出去不会报错，也没有效果**；
- 编辑器右侧的 *Node → Signals* 面板里也能看到并连接它；
- 信号名和参数名会出现在编辑器的补全里。

---

## 6. 第 6 步：用 Rust 写一个节点类型（星星）

到目前为止 `player.rs` 是“挂到已有节点上的脚本”。Rust 也能定义**新的节点类型**
（相当于 GDScript 的 `class_name Star extends Area2D`）。

### 6.1 新建文件 `src/star.rs`

```rust
use godot::classes::{Area2D, IArea2D};   // 基类/接口不在 prelude 里时要自己 use
use godot::prelude::*;

/// 一颗星星：碰到玩家就加分。
#[derive(GodotClass)]
#[class(base = Area2D)]
pub struct Star {
    /// 这颗星星值多少分，检视面板里可改。
    #[export]
    points: i64,

    base: Base<Area2D>,
}

#[godot_api]
impl IArea2D for Star {
    fn init(base: Base<Area2D>) -> Self {
        Self { points: 1, base }
    }

    fn ready(&mut self) {
        godot_print!("Star ready, points = {}", self.points);
    }
}

#[godot_api]
impl Star {
    #[func]
    fn value(&self) -> i64 {
        self.points
    }
}
```

### 6.2 让 crate 知道它

`src/lib.rs` 末尾的注册函数里加一行（`mod star;` 也在文件顶部）：

```rust
mod player;
mod star;

// ... #[gdextension] 那段不用改 ...

pub fn register_scripts() {
    player::register();
    // 节点类型不需要 register_script!，但模块要能扫描到它：
    // 这里留一行注释即可；gdext 会在库加载时自动注册 Star。
}
```

> 小提示：基类如果不在 `godot::prelude` 里（`Area2D`、`CharacterBody2D`、`Control`…），
> 要自己 `use godot::classes::{Area2D, IArea2D};`（类型 + 接口两个名字）。
> 用编辑器 *Attach Script* 建的文件会自动带上这一行，手写文件时需要自己加
> （编译器会提示 `cannot find type` / `cannot find trait`）。
>
> 说明：`#[derive(GodotClass)]` 的类型由 gdext 在扩展加载时自动注册，**不用**写
> `register_script!`。而“可挂载脚本”（第 2 步那种）必须写。

### 6.3 在编辑器里用

> 注意：Rust 的类要**库构建并加载之后**才存在，所以 GDScript 里写 `Star.new()` 之前
> 必须先构建一次（F5 / Rust 面板 Build），否则 GDScript 会报
> `Identifier "Star" not declared in the current scope`。

重新构建后（按 F5 或 Rust 面板的 Build），在场景里 **Add Node** 搜索 `Star` 就能直接加，
检视面板里能看到 `Points`。

**注意**：新增/改名节点类型后需要**重启编辑器**才能刷新类列表（`#[derive(GodotClass)]`
的类不能在同一个进程里注册两次）；只改脚本逻辑则不用，见第 8 章。

### 6.4 小练习

给 `Star` 加一个 `#[export(default = 10.0)] radius: f32`，并在 `ready()` 里打印它。

---

## 7. 第 7 步：把游戏接起来

### 7.1 场景结构

新建场景 `main.tscn`：

```
Main (Node2D)                     ← 挂 main.gd（GDScript）
├── Player (Node2D)               ← 挂 res://src/player.rs（第 3 步那个脚本）
│   └── Sprite2D                  ← 随便指定一张图（可选）
└── Stars (Node2D)                ← 运行时往里塞星星
```

然后在 *项目设置 → 应用 → 运行 → Main Scene* 里选 `main.tscn`。

### 7.2 `main.gd`（GDScript）

```gdscript
extends Node2D

@onready var player := $Player
@onready var stars := $Stars

const STAR := preload("res://src/star.rs")   # 只是为了让编辑器认识它，可不写

func _ready() -> void:
	player.score_changed.connect(_on_score_changed)
	_spawn_star(player.position)                  # 第一颗放在玩家身上，立刻能看到加分
	for i in 4:
		_spawn_star(Vector2(120 + i * 90, 160 + (i % 3) * 70))
	print("stars: ", stars.get_child_count())

func _spawn_star(at: Vector2) -> void:
	var star := Star.new()                    # Rust 节点类型，和内置类一样 new()
	star.position = at
	star.points = 2                           # 导出属性能直接写
	stars.add_child(star)

func _process(_delta: float) -> void:
	# 离得够近就算收集到（不依赖物理引擎，逻辑一眼能看懂）。
	for star in stars.get_children():
		if star.position.distance_to(player.position) < 40.0:
			player.add_score(star.value())    # 调用 Rust 的 #[func]
			star.queue_free()

func _on_score_changed(total: int) -> void:
	print("score = ", total)
```

### 7.3 验证

按 F5，看到 5 颗星星打印 `Star ready, points = 2`；把玩家移到星星旁边（或干脆让某颗星星就放在玩家位置上）就会收集：

```
Player ready, speed = 200
Star ready, points = 2
Star ready, points = 2
Star ready, points = 2
Star ready, points = 2
Star ready, points = 2
stars: 5
score = 2
```

这一章把三种东西串起来了：

- **Rust 脚本**（`Player`）管逻辑；
- **Rust 节点类型**（`Star`）是场景里的实体；
- **GDScript** 负责组装场景和 UI——两边通过 `#[func]`、导出属性、信号互通。

> 不想写 GDScript 也行：这些都能用 Rust 做（`Gd::from_init_fn` 建节点、`connect` 连信号）。
> 用 GDScript 只是演示“两种语言混用”这条路。

---

## 8. 第 8 步：编辑器里的日常

### 8.1 自动补全与跳转（rust-analyzer）

打开任意 `.rs` 文件，模块会在后台起一个 `rust-analyzer`：

- 输入 `self.` 会列出字段和方法；
- **Ctrl/Cmd + 点击**（或 F12）跳到定义；
- 鼠标悬停看类型和文档；
- 保存时的语法错误 / 类型错误直接画在行内，Problems 面板也能看到。

没生效的话看 [07-language-server.md](07-language-server.md)（需要 `rustup component add rust-analyzer`）。

### 8.2 构建与错误

- **F5 / 运行栏 Build / Rust 面板的 Build** 都会先 `cargo build`；
- 编译错误出现在 *Rust* 面板的 **Problems** 页，**双击跳转到出错的行**；
- **Output** 页是 cargo 的原始输出；
- 构建失败时不会启动游戏（除非关掉 `rust/build/before_playing`）。

故意写错一行试试：

```rust
let x: i64 = "hello";   // 类型不匹配
```

**你应该看到**：Problems 里一行 `mismatched types`，双击跳到这一行，游戏不启动。

### 8.3 热重载（不用重启编辑器）

改完 Rust 代码 → 点 Build。日志出现：

```
Rust library reloaded: res://.godot/rust/reload/1/reload.gdextension
```

就说明新代码已经生效：

- **正在运行的脚本实例会被“换心”**：导出属性的值原样保留（改代码不丢状态）；
- 没有重新调用 `ready()`；
- 改了**节点类型**（`#[derive(GodotClass)]`）才需要重启编辑器。

### 8.4 工具脚本（编辑器里也跑）

想在编辑器里就运行（例如自动摆星星、检查资源）：

```rust
#[derive(RustScript)]
#[script(base = Node2D, tool)]     // 加一个 tool
pub struct GridPainter {
    owner: Gd<Node2D>,
}
```

规则和 GDScript 的 `@tool` 一样：不加 `tool` 的脚本在编辑器里只是占位实例（显示属性、不执行）。

---

## 9. 第 9 步：打断点调试

### 9.1 下断点

打开 `src/player.rs`，在 `add_score` 里的 `self.score += points;` 那一行左侧点一下，
出现红点（或右键 → *Toggle Breakpoint*）。

### 9.2 启动

底部 **Rust** 面板 → **Debug** 页 → 点 **Debug**。模块会：

1. 用 debug 档重新构建（带调试信息）；
2. 启动 `lldb-dap`，把游戏挂在它下面跑；
3. 命中断点时自动跳到那一行。

### 9.3 命中之后

- 左栏是**调用栈**（包括 Godot 自己的 C++ 帧，双击可跳）；
- 右栏是当前帧的**变量**（`self`、`points`…）；
- 工具栏：**Continue** 继续、**Step Over / Into / Out** 单步。

第一次用需要 Xcode 命令行工具（`xcode-select --install`）提供 `lldb-dap`。
细节见 [09-debugging.md](09-debugging.md)。

### 9.4 小练习

在 `add_score` 里打断点，撞一颗星星，看 `points` 和 `self.score` 的值，再单步跳过这一行。

---

## 10. 第 10 步：导出给别人玩

1. 编辑器 → *项目 → 导出…* → 新建一个预设（macOS / Windows Desktop / Linux）；
2. 点 **导出项目**；
3. 产物里应当有：

```
MyGame.app/Contents/Resources/libmygame.dylib    ← Rust 动态库（模块自动放好）
MyGame.app/Contents/Resources/MyGame.pck        ← 里面含 Rust 的扩展配置
MyGame.app/Contents/MacOS/MyGame                ← 游戏本体
```

同一平台导出时模块会自动构建 + 打包；跨平台导出需要自己交叉编译，见
[08-exporting.md](08-exporting.md)。

### 10.1 验证导出

直接双击打开的 app，或命令行跑：

```sh
open MyGame.app
# 或者看输出：
"MyGame.app/Contents/MacOS/MyGame" --headless --quit-after 60
```

看到 `Player ready, speed = 200` 之类的输出就说明 Rust 库加载成功了。

---

## 11. 完整代码清单

把这一章的文件抄进项目，就是一个能跑的完整例子。

### 11.1 `src/lib.rs`

```rust
use godot::prelude::*;

mod player;
mod star;

/// 扩展入口：一个 GDExtension 只能有一个。
struct CollectStars;

#[gdextension]
unsafe impl ExtensionLibrary for CollectStars {
    fn on_stage_init(stage: InitStage) {
        if stage == InitStage::Scene {
            register_scripts();
        }
    }
}

/// 所有可挂载脚本都在这里注册（编辑器加脚本时会自动补）。
pub fn register_scripts() {
    player::register();
}
```

### 11.2 `src/player.rs`

```rust
use godot::classes::Input;
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D)]
pub struct Player {
    owner: Gd<Node2D>,

    #[export(default = 200.0, range = (0.0, 600.0))]
    speed: f32,

    #[export]
    score: i64,
}

#[godot_script_api]
impl Player {
    fn ready(&mut self) {
        godot_print!("Player ready, speed = {}", self.speed);
    }

    fn process(&mut self, delta: f64) {
        let input = Input::singleton();
        let mut direction = Vector2::ZERO;
        if input.is_action_pressed("ui_right") {
            direction.x += 1.0;
        }
        if input.is_action_pressed("ui_left") {
            direction.x -= 1.0;
        }
        if input.is_action_pressed("ui_down") {
            direction.y += 1.0;
        }
        if input.is_action_pressed("ui_up") {
            direction.y -= 1.0;
        }
        if direction == Vector2::ZERO {
            return;
        }
        let mut node = self.owner.clone();
        let position = node.get_position();
        node.set_position(position + direction.normalized() * self.speed * delta as f32);
    }

    #[func]
    fn add_score(&mut self, points: i64) -> i64 {
        self.score += points;
        self.owner.clone().emit_signal("score_changed", &[self.score.to_variant()]);
        self.score
    }

    #[func]
    fn boost(&mut self, points: i64, #[opt(default = 1.0)] multiplier: f32) -> i64 {
        let gained = (points as f32 * multiplier).round() as i64;
        self.score += gained;
        self.owner.clone().emit_signal("score_changed", &[self.score.to_variant()]);
        self.score
    }

    #[signal]
    fn score_changed(total: i64) {}
}

godot_script::register_script!(Player);
```

### 11.3 `src/star.rs`

```rust
use godot::classes::{Area2D, IArea2D};
use godot::prelude::*;

#[derive(GodotClass)]
#[class(base = Area2D)]
pub struct Star {
    #[export]
    points: i64,

    base: Base<Area2D>,
}

#[godot_api]
impl IArea2D for Star {
    fn init(base: Base<Area2D>) -> Self {
        Self { points: 1, base }
    }

    fn ready(&mut self) {
        godot_print!("Star ready, points = {}", self.points);
    }
}

#[godot_api]
impl Star {
    #[func]
    fn value(&self) -> i64 {
        self.points
    }
}
```

### 11.4 `main.gd`（挂在 `Main` 节点上）

```gdscript
extends Node2D

@onready var player := $Player
@onready var stars := $Stars

func _ready() -> void:
	player.score_changed.connect(_on_score_changed)
	_spawn_star(player.position)                  # 第一颗放在玩家身上，立刻能看到加分
	for i in 4:
		_spawn_star(Vector2(120 + i * 90, 160 + (i % 3) * 70))
	print("stars: ", stars.get_child_count())

func _spawn_star(at: Vector2) -> void:
	var star := Star.new()
	star.position = at
	star.points = 2
	stars.add_child(star)

func _process(_delta: float) -> void:
	for star in stars.get_children():
		if star.position.distance_to(player.position) < 40.0:
			player.add_score(star.value())
			star.queue_free()

func _on_score_changed(total: int) -> void:
	print("score = ", total)
```

### 11.5 验证清单

| 检查项 | 期望 |
| --- | --- |
| 构建 | Rust 面板显示 `Finished \`dev\` profile` |
| 运行 | 控制台 `Player ready, speed = 200` |
| 星星 | 5 行 `Star ready, points = 2` + `stars: 5` |
| 收集 | 走近星星打印 `score = 2` |
| 检视面板 | `Player` 有 Speed（滑条）/ Score；`Star` 有 Points |
| 编辑器里调用 | GDScript 里 `player.add_score(5)` 立即加分 |

---

## 12. 下一步与常见问题

### 12.1 继续深入

| 想做的事 | 看这里 |
| --- | --- |
| 属性提示、类型、导出清单 | [03-writing-scripts.md](03-writing-scripts.md) |
| 脚本编辑器里的完整工作流 | [04-editor-workflow.md](04-editor-workflow.md) |
| 补全 / 跳转 / 实时诊断 | [07-language-server.md](07-language-server.md) |
| 热重载的原理与限制 | [04-editor-workflow.md](04-editor-workflow.md) §5.1 |
| 断点调试全部细节 | [09-debugging.md](09-debugging.md) |
| 导出到别的平台 | [08-exporting.md](08-exporting.md) |
| 模块内部实现 | [06-architecture.md](06-architecture.md) |

### 12.2 常见问题速查

| 现象 | 原因 / 解决 |
| --- | --- |
| `error[E0433]: cannot find module or crate godot_script` | `Cargo.toml` 里的依赖没同步。重新打开一次编辑器或点 Build，模块会自动写回；还不行就看 [02-project-setup.md](02-project-setup.md) |
| 编辑器里搜不到 `Star` 节点 | 节点类型要**重启编辑器**才会刷新类列表 |
| GDScript 报 `Identifier "Star" not declared in the current scope` | Rust 库还没构建/加载。先构建一次，再重新打开这个脚本 |
| 改了 `#[func]` 但 GDScript 里报没有这个方法 | 没重新构建，或构建失败（看 Problems 面板） |
| 编辑器里脚本完全不执行 | 非 `tool` 脚本在编辑器里不运行（和 GDScript 一致），按 F5 跑游戏才执行 |
| 断点命中不了 | 见 [09-debugging.md](09-debugging.md) §5 |
| `Method 'X' has changed and no compatibility fallback…` | 引擎和已有动态库版本不匹配。重新构建项目（模块也会自动重建过期的库） |
| 控制台没有 `godot_print!` 输出 | 无头/编辑器模式下输出在 **Output** 面板和编辑器日志里；游戏运行时在游戏控制台 |
| 构建很慢 | 第一次要编译 gdext（几分钟），之后是增量；cargo 的缓存目录在 `.godot/rust/target` |

### 12.3 已经掌握的东西

到这里你已经用到了这个模块的全部核心能力：

- 可挂载脚本（属性 / 生命周期 / 方法 / 信号）；
- 与 GDScript 双向互调；
- Rust 节点类型；
- 编辑器集成（补全、错误定位、热重载、调试）；
- 导出发布。

剩下的就是把它用在你自己的游戏上。祝顺利 🎮

