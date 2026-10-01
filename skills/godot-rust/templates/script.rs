// 脚本骨架：复制到 <project>/src/<name>.rs，改类名/基类，
// 然后在 src/lib.rs 里加 `mod <name>;` 和 `<name>::register();`。
//
// 命名：文件名用 snake_case（合法 Rust 模块名，不能有 '-'、不能以数字开头），
//       结构体名用 UpperCamelCase。
use godot::classes::Node2D; // 基类不在 godot::prelude 时必须显式 import
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D)] // tool → 编辑器里也运行；name = "Xxx" → 自定义类名
pub struct MyScript {
    /// 名字为 owner 的字段自动拿到挂载的节点（只是引用，clone() 不复制节点）。
    owner: Gd<Node2D>,

    /// 导出到检视面板并保存进场景。
    #[export]
    speed: f32,

    /// 初始值不是 Default::default() 时用 default（类型必须与字段一致）。
    #[export(default = 1.0, range = (0.0, 100.0, 0.5))]
    gravity: f32,

    /// 不导出的普通字段。
    elapsed: f64,
}

#[godot_script_api]
impl MyScript {
    // ---- 生命周期：按名字识别，签名必须一致，不要加 #[func] ----

    fn ready(&mut self) {
        godot_print!("MyScript ready");
    }

    fn process(&mut self, delta: f64) {
        self.elapsed += delta;
    }

    fn physics_process(&mut self, _delta: f64) {}

    fn enter_tree(&mut self) {}

    fn exit_tree(&mut self) {}

    // ---- 暴露给 GDScript / 编辑器的方法 ----

    #[func]
    fn jump(&mut self, height: f32) -> f32 {
        self.speed + height
    }

    #[func]
    fn boost(&mut self, amount: f32, #[opt(default = 1.0)] factor: f32) -> f32 {
        amount * factor
    }

    // ---- 信号 ----

    #[func]
    fn do_jump(&mut self) {
        let height: f32 = 5.0;
        self.owner
            .clone()
            .emit_signal("jumped", &[height.to_variant()]);
    }

    #[signal]
    fn jumped(height: f32) {}
}

godot_script::register_script!(MyScript);
