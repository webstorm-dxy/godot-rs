# 08 · 导出（把 Rust 脚本打进游戏）

编辑器里跑得好好的，导出后却“没有 Rust”——这是 GDExtension 项目的常见坑：`.gdextension`
和动态库不会自动进导出产物。这个模块带了导出插件，点“导出项目”就会：

1. 按导出预设的 **Debug/Release** 重新 `cargo build`（release 预设会用 `--release`）；
2. 把动态库放到导出产物里 `res://` 能解析到的位置（macOS 是 `App.app/Contents/Resources/`，
   Windows/Linux 是可执行文件旁边）；
3. 往 pck 里写一份只针对该平台的 `.godot/rust/rust.gdextension`。

导出日志里会看到：

```
Rust: packaged libmygame.dylib (macos, release) into the export.
```

## 1. 编辑器里怎么导出

0. 先装好**导出模板**（编辑器 *编辑器 → 管理导出模板…*，或从 godotengine.org 下载对应版本），
   这是 Godot 的通用要求，和 Rust 无关；
1. **项目 → 导出…**，新建一个预设（例如 macOS / Windows Desktop / Linux/X11）；
2. 选择导出路径，点 **导出项目**（或 **导出 PCK/ZIP**，两者都会带上 Rust 库）；
3. 产物里应当能看到：

```
MyGame.app/Contents/Resources/libmygame.dylib      ← 动态库
MyGame.app/Contents/Resources/MyGame.pck           ← 里面含 .godot/rust/rust.gdextension
MyGame.app/Contents/MacOS/MyGame                   ← 游戏本体
```

Windows/Linux 上库会放在可执行文件同目录（`libmygame.so` / `mygame.dll`）。

## 2. 支持的平台

| 情况 | 行为 |
| --- | --- |
| 导出平台 = 编辑器所在平台（macOS/Windows/Linux） | 自动构建并打包 ✅ |
| 导出平台不同（例如在 macOS 上导出 Windows） | 打一条警告并跳过 Rust 部分：需要交叉编译后手动放进预设 ⚠️ |
| Android / iOS / Web | 目前不支持（模块只处理桌面平台） |

交叉编译的做法（简版）：

```sh
# 例：在 macOS 上给 Linux 构建
rustup target add x86_64-unknown-linux-gnu
cargo build --release --target x86_64-unknown-linux-gnu \
    --manifest-path /path/to/MyGame/Cargo.toml
```

然后把产物 `.so`/`.dll` 通过导出预设的 **Filters to export non-resource files/folders**
（或者你自己的 `EditorExportPlugin`）放进导出目录，并保证它相对可执行文件的位置与
`.gdextension` 里的路径一致。

## 3. 导出的游戏是怎么找到扩展的

模块启动时（Scene 级初始化）会自己加载 `res://.godot/rust/rust.gdextension`：

- 开发时：这个文件在项目磁盘上，内容是编辑器生成的（指向 `.godot/rust/target/...`）；
- 导出后：它在 pck 里，内容是导出插件写的，指向 `res://libmygame.dylib`，
  而 `res://` 在导出游戏里解析到可执行文件（macOS 是 `Contents/Resources`）旁。

因此**不需要**手动维护 `extension_list.cfg`，也不用把 `.gdextension` 放进项目树。

## 4. 常见问题

### 导出日志出现 `Rust: exporting for 'windows' from a 'macos' editor ...`

这是跨平台导出：模块不会替你交叉编译。按上一节手动构建并放进导出目录。

### 导出的游戏里脚本没生效

按顺序检查：

1. 导出目录里有没有那个动态库（`.dylib`/`.so`/`.dll`）；
2. 动态库位置是不是 `res://` 解析到的目录（见上面第 1 节）；
3. 运行导出的游戏时加 `--verbose`，看有没有 `Rust extension loaded` /
   `godot-script: registered ...`；
4. 库和游戏是不是同一次构建（改了代码要重新导出）。

### 我用了自定义导出插件 / 自己管资源

只要保证两件事：动态库在 `res://` 能解析到的位置；pck 里的
`.godot/rust/rust.gdextension` 指向它。模块不再做别的事。
