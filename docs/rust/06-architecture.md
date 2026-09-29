# 06 · 内部原理、支持范围与路线图

这一篇写给想改引擎/模块的人，初学者可以先跳过。

## 1. 模块结构

```
modules/rust/
├── rust_language.*              # ScriptLanguage：高亮、校验、全局类名
├── rust_script.*                # .rs 对应的 Script 资源（挂载的载体）
├── rust_script_registry.*       # Rust 类型注册表（引擎单例 RustScriptRegistry）
├── rust_script_resource_format.*# .rs 的加载/保存（保存时自动写入 crate 根）
├── rust_paths.*                 # .godot/rust 下的路径约定
├── rust_diagnostics.*           # 构建诊断（供编辑器显示行内错误）
│                                # （描述符缓存 script_cache.json 由 rust_project 读写）
├── editor/                      # 构建集成（仅编辑器构建编译）
│   ├── rust_project.*           # 脚手架、.gdextension、设置
│   ├── rust_build.*             # cargo 调用 + JSON 诊断 + 线程
│   ├── rust_build_panel.*       # Rust Dock（Problems/Output）
│   ├── rust_bindings.*          # 导出本引擎的 extension_api.json
│   └── rust_editor_plugin.*     # 运行栏按钮、F5 前构建、加载扩展
├── support/godot-script/        # Rust 侧运行时 crate（可挂载脚本 API）
├── support/godot-script-derive/ # 派生宏：#[derive(RustScript)] / #[godot_script_api]
└── vendor/gdext/                # 内置的 godot-rust 0.5.5（未修改，MPL-2.0）
```

## 2. 一次挂载脚本的生命周期

```
[Rust] register_script!(Player)
   │  通过引擎单例 RustScriptRegistry 调用 register_script_type(
   │      path, class_name, base, is_tool, create_fn, descriptor)
   │      descriptor ＝ { properties, methods, signals }
   ▼
[C++] RustScriptRegistry            （HashMap<res:// 路径, ScriptType>）
   │
   │  场景加载：节点带着 script = res://src/player.rs
   ▼
[C++] Object::set_script() → RustScript::instance_create(obj)
   │  1. 用路径查注册表；2. 校验 obj 是否 is-a base
   │  3. 调用 Rust 的 create_fn(owner_id, script_id, language_id)
   ▼
[Rust] create_instance::<Player>()
   │  用 instance id 还原 Gd<Node2D> / Gd<Script> / Gd<ScriptLanguage>
   │  构造 ScriptInstanceHandle<Player>（实现 gdext 的 ScriptInstance）
   │  create_script_instance(handle, owner) → GDExtensionScriptInstancePtr
   ▼
[C++] gdextension_script_instance_wrap(ptr) → ScriptInstance* 返回给引擎
   │
   ▼
[引擎] 调用属性读写、has_method/call（_ready/_process/... 以及 methods()/signals()）
      全部由 ScriptInstanceHandle 转发到你的 Rust 类型
```

要点：**没有解析 `.rs` 源码**。类名、基类、属性、方法、信号都来自 Rust 编译期生成并
注册的描述符；因此“构建过”是脚本能实例化的前提。

## 2.1 描述符缓存（未构建时也能显示）

构建成功后，编辑器会把注册表里的描述符写成 `.godot/rust/script_cache.json`（整个文件
由 `.godot/` 忽略规则管着，不进版本库）。下次打开编辑器时：

1. 若扩展已加载，Rust 侧会重新注册，缓存里同名的条目被跳过（库里的信息更新）；
2. 若还没构建/没加载（例如刚 clone 下来、或构建失败），`RustScript` 用缓存回答
   `get_instance_base_type()` / `get_script_property_list()` / `get_script_method_list()` /
   `get_script_signal_list()`，检视面板和“连接信号”对话框因此不会空白；
3. 缓存条目的 `create_fn` 是 0，所以 `can_instantiate()` 仍然是 false，编辑器用占位实例
   （只读显示属性），运行游戏前仍必须构建成功。

文件结构：

```json
{
  "version": 1,
  "scripts": {
    "res://src/player.rs": {
      "class_name": "Player",
      "base": "Node2D",
      "is_tool": false,
      "properties": [{ "name": "speed", "type": 3, "hint": 0, "hint_string": "", "usage": 6 }],
      "methods": [{ "name": "jump", "return_type": 3, "args": [{ "name": "height", "type": 3 }] }],
      "signals": [{ "name": "jumped", "args": [{ "name": "height", "type": 3 }] }]
    }
  }
}
```

（`type` 就是 Godot 的 `Variant::Type` 编号，例如 3 = float、4 = String。）

## 3. 为什么要自己装载扩展

引擎的扩展列表 `.godot/extension_list.cfg` 由编辑器根据 `res://` 下扫描到的
`.gdextension` 文件重写。Rust 模块把配置生成在 `.godot/rust/` 里（不出现在项目树中），
所以在所有构建模式下由模块自己 `GDExtensionManager::load_extension()` 加载。

## 4. 当前支持范围

已支持：

- 新建项目自动生成 Cargo 工程、绑定、扩展配置；
- F5 / `--build-solutions` 前自动 `cargo build`；错误在 Problems 面板与日志中可点击跳转；
- `.rs` 文件在编辑器里打开、高亮、行内错误；
- 创建脚本时自动维护 `src/lib.rs` 的 `mod` / `register`；
- 可挂载脚本：类名、基类校验、`new/ready/process/physics_process/enter_tree/exit_tree`、
  `properties/get_property/set_property`（检视面板 + 场景序列化）；
- 方法与信号：`methods()/call_method()` 可从 GDScript 调用（支持 `#[opt(default = ...)]`
  默认参数），`signals()` 可连接、可发射；
- 工具脚本：`#[script(base = X, tool)]`；和 GDScript 一样，编辑器里只实例化 tool 脚本
  （其余用占位实例，`can_instantiate()` 与 `ScriptServer::is_scripting_enabled()` 一致）；
- 占位实例：库未构建时挂载不报错，构建后生效；
- 描述符缓存：`.godot/rust/script_cache.json`，未构建时也能显示类名/属性/方法/信号；
- 派生宏：`#[derive(RustScript)]`（字段、`#[export]` 及 `range/enum/flags/file/dir/multiline`
  等提示、`#[export(storage)]`）+ `#[godot_script_api]`（生命周期、`#[func]`、`#[signal]`），
  生成的代码就是下面的手写 API；
- 内置 rust-analyzer：补全、悬停、跳转定义、实时诊断（见 [07-language-server.md](07-language-server.md)）。

尚未实现（路线图）：

| 项 | 说明 |
| --- | --- |
| RPC | `@rpc` 风格的多人同步 |
| 热重载 | 库重载后的类与实例状态迁移（对应计划里的 M4） |
| 原生断点调试 | lldb-dap / CodeLLDB（M5） |
| 导出 | 把 cdylib 打进导出产物（M6） |

## 5. 修改模块时的注意事项

- 语言层的分隔符必须“全部由符号组成且不重复”，否则编辑器会报错（见
  [05-troubleshooting.md](05-troubleshooting.md)）；
- Rust 侧与 C++ 侧通过**实例 ID**（不是裸指针）交换对象，避免悬垂；
- `RustScriptRegistry` 是引擎单例，扩展在 Scene 级初始化时注册，重载扩展时需重新注册
  （热重载落地时要处理）；
- `vendor/gdext` 保持不修改：绑定由本引擎导出的 JSON 生成（`api-custom-json`），
  这样改了引擎也不需要 patch 绑定 crate。
