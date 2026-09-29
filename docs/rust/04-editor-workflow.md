# 04 · 编辑器工作流

## 1. 启动

```sh
./bin/godot.macos.editor.arm64.rust --path /path/to/MyGame -e
```

（`-e` 表示以编辑器打开；不加就是直接运行游戏。）

## 2. 附加脚本到节点

1. 在场景树里**右键节点** → **Attach Script…**（或点节点面板右上角的脚本图标）；
2. Language 选 **Rust**；
3. 填类名（例如 `Player`）与路径（默认 `res://src/player.rs`）；
4. **Create**。

编辑器会：

- 生成脚本文件（模板里已填好 `type Base = <节点基类>`）；
- 自动把 `mod player;` 与 `player::register();` 写进 `src/lib.rs`；
- 把脚本挂到该节点上（此时是占位实例，因为还没编译）。

## 3. 构建

三种方式，效果一样：

1. **运行栏左侧的锤子/勾选图标**（Build Rust project）——只构建；
2. **F5**（运行主场景）——先构建，失败就中止运行；
3. 命令行：

   ```sh
   ./bin/godot.macos.editor.arm64.rust --headless --path /path/to/MyGame \
       --build-solutions --quit
   ```

构建命令实际是：

```sh
cargo build --manifest-path <项目>/Cargo.toml \
    --message-format=json-render-diagnostics \
    --config build.target-dir="<项目>/.godot/rust/target" \
    --config env.GDRUST_GODOT_API_JSON.value="<项目>/.godot/rust/bindings/extension_api.json" \
    ...
```

产物是 `.godot/rust/target/debug/lib<项目名>.dylib`，由模块自动加载。

## 4. 看错误

- **Rust 面板**（底部 Dock，标题 *Rust*）：
  - *Problems* 页：错误/警告列表，**双击跳转到 `.rs` 的行列**；
  - *Output* 页：cargo 的原始输出；
- **Output/编辑器日志**：同样可点击定位（形如 `res://src/player.rs:12`）；
- 无头模式（CI）下 cargo 输出会直接打印到控制台。

构建失败时**不会启动游戏**（除非把 `rust/build/before_playing` 关掉）。

## 5. 运行

- **F5**：构建 → 启动游戏子进程；
- 只改 Rust 代码也要重新构建：F5 会自动做；
- 编辑器内的 Rust 类定义：改完代码后，编辑器里的类信息在**重新加载扩展或重启编辑器**
  后才更新（运行游戏始终用最新的库）。

## 6. 新建节点类型（另一种用法）

除了“挂载到已有节点”，你也可以用 gdext 的 `#[derive(GodotClass)]` 定义**自己的节点类型**：

```rust
#[derive(GodotClass)]
#[class(base = Node2D)]
struct Bullet {
    #[export]
    speed: f32,
    base: Base<Node2D>,
}

#[godot_api]
impl INode2D for Bullet {
    fn init(base: Base<Node2D>) -> Self {
        Self { speed: 400.0, base }
    }
}
```

构建后它会出现在 **Add Node** 对话框里，场景可以直接用，GDScript 也能 `extends Bullet`。

两种方式的区别：

| | 可挂载脚本（本文档主线） | 节点类型（gdext `GodotClass`） |
| --- | --- | --- |
| 挂到已有节点 | ✅ | ❌（它自己就是节点类型） |
| 出现在 Add Node 列表 | ❌ | ✅ |
| 组合已有节点 | 通过 `owner` 访问 | 通过 `Base<T>` / `#[export] Gd<T>` |
