# 01 · 安装与编译

## 1. 环境要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | 目前只在 **macOS (Apple Silicon)** 上验证过；代码路径兼容 Linux/Windows，但未实测 |
| Rust | `cargo` / `rustc` **1.94 或更新**（随引擎内置的 gdext 0.5.5 要求；本机 1.95 已验证） |
| C++ 工具链 | Apple Command Line Tools（Apple Clang 16+） |
| Python + SCons | 编译 Godot 本身需要（仓库自带 `SConstruct`） |

检查 Rust：

```sh
cargo --version   # cargo 1.95.0 之类
rustc --version
```

> 如果 `cargo` 不在 `PATH` 里（比如从 Finder 启动编辑器），可以在编辑器设置里填
> `rust/cargo_path`，或设置环境变量 `GODOT_RUST_CARGO`。

## 2. 编译编辑器

仓库根目录的 `build-macos.sh` 已经打开 Rust 模块：

```sh
./build-macos.sh
```

产物是 **`bin/godot.macos.editor.arm64.rust`**。

注意两点：

1. 名字末尾的 `.rust` 是模块后缀（和 C# 模块的 `.mono` 同一机制）。**不要用**旧的
   `bin/godot.macos.editor.arm64`，那个二进制里没有 Rust 模块。
2. 如果 shell 的 `PATH` 把 Homebrew LLVM 放在最前面，编译会报
   `could not build module 'std_float_h'`。`build-macos.sh` 已经自动把 Command Line
   Tools 放到最前面，直接用脚本构建即可。

想手动调用 scons 时，记得带上模块开关：

```sh
scons platform=macos arch=arm64 target=editor module_rust_enabled=yes
```

## 3. 验证安装

```sh
./bin/godot.macos.editor.arm64.rust --headless --version
# 4.7.2.stable.rust.custom_build.xxxxxxxx
```

版本串里出现 `rust` 就说明模块已经编进去了。

## 4. 引擎需要网络吗？

引擎本身不需要。**用户项目第一次构建**时需要网络，因为 cargo 要：

- 从 crates.io 拉取依赖（`nanoserde`、`which`、`syn` 等）；
- 拉取上游 `godot4-prebuilt` 仓库（gdext 的绑定来源；我们用本引擎生成的绑定，
  这个依赖会被忽略，但 cargo 仍要解析它）。

之后都会缓存在 `CARGO_HOME` 里。可以先在项目目录里跑一次 `cargo fetch` 预热。
