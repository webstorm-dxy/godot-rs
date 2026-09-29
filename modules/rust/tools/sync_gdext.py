#!/usr/bin/env python3
"""Re-vendor the godot-rust (gdext) workspace for modules/rust.

Usage:
    python3 modules/rust/tools/sync_gdext.py --tag v0.5.6 [--repo URL]

Re-applies every patch found in modules/rust/vendor/patches/*.patch (if any).
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True, help="Upstream git tag, e.g. v0.5.6")
    parser.add_argument("--repo", default="https://github.com/godot-rust/gdext")
    args = parser.parse_args()

    module_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    vendor_dir = os.path.join(module_dir, "vendor", "gdext")
    patches_dir = os.path.join(module_dir, "vendor", "patches")
    tmp_dir = os.path.join(module_dir, "vendor", ".gdext-tmp")

    if os.path.exists(tmp_dir):
        shutil.rmtree(tmp_dir)

    print(f"Cloning {args.repo} at {args.tag}...")
    subprocess.check_call(
        ["git", "clone", "--quiet", "--depth", "1", "--branch", args.tag, args.repo, tmp_dir]
    )
    shutil.rmtree(os.path.join(tmp_dir, ".git"), ignore_errors=True)
    shutil.rmtree(os.path.join(tmp_dir, ".github"), ignore_errors=True)

    commit = subprocess.check_output(["git", "-C", tmp_dir, "rev-parse", "HEAD"], text=True).strip() \
        if os.path.isdir(os.path.join(tmp_dir, ".git")) else "unknown"

    if os.path.exists(vendor_dir):
        shutil.rmtree(vendor_dir)
    shutil.move(tmp_dir, vendor_dir)

    for patch in sorted(glob.glob(os.path.join(patches_dir, "*.patch"))):
        print(f"Applying {os.path.basename(patch)}...")
        subprocess.check_call(["git", "apply", patch], cwd=vendor_dir)

    print(f"Vendored gdext {args.tag} into {vendor_dir}")
    print("Update vendor/UPSTREAM.md with the new tag/commit reference.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
