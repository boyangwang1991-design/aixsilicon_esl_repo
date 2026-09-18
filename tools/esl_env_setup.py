"""esl_env_setup：ESL 核心环境安装（K13 esl-environment-setup 的确定性工具）。

安装目标（用户级，无需 root，前缀默认 ~/.local）：
- cmake：官方预编译二进制（GitHub releases，Linux x86_64）。
- systemc：Accellera 源码 configure/make/install（SystemC 2.3.4 或指定版本）。

用法：
  uv run python tools/esl_env_setup.py install --prefix ~/.local --systemc-version 2.3.4
  uv run python tools/esl_env_setup.py status

注意：只安装到用户目录；系统级安装需显式授权。具体版本由工程环境固定。
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

CMAKE_VERSION = "3.28.6"
SYSTEMC_VERSION = "2.3.4"   # Accellera 稳定版（SystemC 2.3.x 兼容 gcc 8）
GITHUB = "https://github.com"


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, check=True, **kw)


def ensure_url_download(url, dest: Path, timeout=300):
    if dest.exists() and dest.stat().st_size > 100_000:
        print(f"  cached: {dest}")
        return dest
    run(["curl", "-fsSL", "--connect-timeout", "20", "--max-time", str(timeout),
         "-o", str(dest), url])
    return dest


def install_cmake(prefix: Path) -> Path:
    print(f"[cmake] 安装 {CMAKE_VERSION} 到 {prefix}")
    arch = "x86_64"
    name = f"cmake-{CMAKE_VERSION}-linux-{arch}"
    tarball = prefix / "downloads" / f"{name}.tar.gz"
    ensure_url_download(f"{GITHUB}/Kitware/CMake/releases/download/v{CMAKE_VERSION}/{name}.tar.gz",
                        tarball)
    with tarfile.open(tarball, "r:gz") as tf:
        tf.extractall(prefix, filter="data")
    cmake = prefix / name / "bin" / "cmake"
    if not cmake.exists():
        raise RuntimeError("cmake 解压后未找到可执行文件")
    print(f"[cmake] OK: {cmake}")
    return cmake


def install_systemc(prefix: Path, version: str) -> Path:
    print(f"[systemc] 安装 {version} 到 {prefix}/systemc")
    systemc_prefix = prefix / "systemc"
    src_dir = prefix / "systemc-src"
    if src_dir.exists():
        shutil.rmtree(src_dir)
    tarball = prefix / "downloads" / f"systemc-{version}.tar.gz"
    # Accellera 官方发布（GitHub mirror: accellera-official/systemc）
    ensure_url_download(
        f"{GITHUB}/accellera-official/systemc/archive/refs/tags/{version}.tar.gz",
        tarball)
    src_dir.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tarball, "r:gz") as tf:
        tf.extractall(src_dir, filter="data")
    build_dir = src_dir / f"systemc-{version}"
    if not build_dir.exists():
        # 某些 tag 目录名不同
        candidates = [p for p in src_dir.iterdir() if p.is_dir()]
        build_dir = candidates[0] if candidates else build_dir
    run(["bash", "configure", "--prefix", str(systemc_prefix)],
        cwd=build_dir, env={**os.environ})
    run(["make", "-j", str(os.cpu_count() or 4)], cwd=build_dir)
    run(["make", "install"], cwd=build_dir)
    lib = systemc_prefix / "lib-linux64" / "libsystemc.a"
    if not lib.exists():
        raise RuntimeError(f"SystemC 库未生成: {lib}")
    print(f"[systemc] OK: {systemc_prefix} (libsystemc.a)")
    return systemc_prefix


def cmd_status(prefix: Path):
    print("=== ESL 核心环境状态 ===")
    for name, paths in [
        ("python", [Path(sys.executable)]),
        ("cmake", [prefix / f"cmake-{CMAKE_VERSION}-linux-x86_64/bin/cmake"]),
        ("systemc.h", [prefix / "systemc/include/systemc.h"]),
        ("libsystemc.a", [prefix / "systemc/lib-linux64/libsystemc.a"]),
    ]:
        hit = next((p for p in paths if p.exists()), None)
        print(f"  {name}: {hit if hit else '缺失'}")
    import shutil as _sh
    gxx = _sh.which("g++")
    print(f"  g++: {gxx if gxx else '缺失'}")


def cmd_install(prefix: Path, systemc_version: str, only: str | None):
    prefix.mkdir(parents=True, exist_ok=True)
    (prefix / "downloads").mkdir(parents=True, exist_ok=True)
    if only in (None, "cmake"):
        install_cmake(prefix)
    if only in (None, "systemc"):
        install_systemc(prefix, systemc_version)
    cmd_status(prefix)
    print("\n环境变量建议（加入 ~/.bashrc）：")
    print(f'  export PATH="{prefix}/cmake-{CMAKE_VERSION}-linux-x86_64/bin:$PATH"')
    print(f'  export SYSTEMC_HOME="{prefix}/systemc"')
    print(f'  export LD_LIBRARY_PATH="{prefix}/systemc/lib-linux64:$LD_LIBRARY_PATH"')


def main(argv=None):
    ap = argparse.ArgumentParser(prog="esl_env_setup", description="ESL 核心环境安装")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_install = sub.add_parser("install", help="安装 cmake/systemc")
    p_install.add_argument("--prefix", default=str(Path.home() / ".local"))
    p_install.add_argument("--systemc-version", default=SYSTEMC_VERSION)
    p_install.add_argument("--only", choices=["cmake", "systemc"], default=None)
    p_install.set_defaults(func=cmd_install)
    p_status = sub.add_parser("status", help="查看状态")
    p_status.add_argument("--prefix", default=str(Path.home() / ".local"))
    p_status.set_defaults(func=lambda a: cmd_status(Path(a.prefix)))
    args = ap.parse_args(argv)
    if args.cmd == "install":
        args.func(Path(args.prefix), args.systemc_version, args.only)
    else:
        args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
