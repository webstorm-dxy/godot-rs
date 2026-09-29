def can_build(env, platform):
    return True


def configure(env):
    env.add_module_version_string("rust")


def is_enabled():
    # Enabled by default; disable with module_rust_enabled=no.
    return True
