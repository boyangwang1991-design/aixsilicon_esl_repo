"""esl_env_setup：ESL 核心环境安装（K13 esl-environment-setup 的确定性工具）。

安装目标（用户级，无需 root，前缀默认 ~/.local）：
- cmake：官方预编译二进制（GitHub releases，Linux x86_64）。
- systemc：Accellera 源码 CMake 构建（版本由 contracts/environment.json 固定）。

用法：
  uv run python tools/esl_env_setup.py install --prefix ~/.local --only systemc
  uv run python tools/esl_env_setup.py status

注意：只安装到用户目录；系统级安装需显式授权。具体版本由工程环境固定。
"""
from __future__ import annotations

import argparse
import gzip
import json
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

CMAKE_VERSION = "3.28.6"
ENVIRONMENT = json.loads((Path(__file__).resolve().parents[1] / "contracts/environment.json").read_text())
SYSTEMC_VERSION = ENVIRONMENT["systemc_version"]
GITHUB = "https://github.com"


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, check=True, **kw)


def ensure_url_download(url, dest: Path, timeout=300):
    def complete_archive(path):
        try:
            # Reading to EOF checks the gzip trailer; size alone accepts partial downloads.
            with gzip.open(path, "rb") as stream:
                while stream.read(1024 * 1024):
                    pass
            with tarfile.open(path, "r:gz") as archive:
                return bool(archive.getmembers())
        except (OSError, EOFError, tarfile.TarError):
            return False

    if dest.exists() and complete_archive(dest):
        print(f"  cached: {dest}")
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    partial = dest.with_suffix(dest.suffix + ".part")
    try:
        run(["curl", "-fsSL", "--connect-timeout", "20", "--max-time", str(timeout),
             "-o", str(partial), url])
        if not complete_archive(partial):
            raise RuntimeError(f"下载的压缩包不完整: {partial}")
        partial.replace(dest)
    finally:
        partial.unlink(missing_ok=True)
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


def install_systemc(prefix: Path, version: str, install_prefix: Path | None = None) -> Path:
    prefix = prefix.resolve()
    if version != SYSTEMC_VERSION:
        raise ValueError(f"目标版本固定为 {SYSTEMC_VERSION}；升级先更新 contracts/environment.json")
    systemc_prefix = (install_prefix or prefix / "systemc").resolve()
    print(f"[systemc] 安装 {version} 到 {systemc_prefix}")
    src_dir = prefix / f"systemc-src-{version}"
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
    cmake = shutil.which("cmake")
    if not cmake:
        candidate = prefix / f"cmake-{CMAKE_VERSION}-linux-x86_64/bin/cmake"
        if candidate.exists():
            cmake = str(candidate)
    if not cmake:
        raise RuntimeError("缺 CMake：先准备环境，不能继续 SystemC 构建")
    binary_dir = src_dir / "build"
    run([cmake, "-S", str(build_dir), "-B", str(binary_dir),
         f"-DCMAKE_INSTALL_PREFIX={systemc_prefix}",
         f"-DCMAKE_CXX_STANDARD={ENVIRONMENT['cxx_standard']}",
         "-DCMAKE_BUILD_TYPE=Release", "-DENABLE_EXAMPLES=OFF"])
    run([cmake, "--build", str(binary_dir), "--parallel", str(min(os.cpu_count() or 4, 8))])
    run([cmake, "--install", str(binary_dir)])
    packages = list(systemc_prefix.rglob("SystemCLanguageConfig.cmake"))
    if not packages:
        raise RuntimeError(f"SystemC CMake package 未生成: {systemc_prefix}")
    print(f"[systemc] 已安装，仍须消费者编译/运行验证: {systemc_prefix}")
    return systemc_prefix


def cmd_status(prefix: Path, systemc_prefix: Path | None = None):
    systemc_prefix = systemc_prefix or prefix / "systemc"
    print("=== ESL 核心环境状态 ===")
    for name, paths in [
        ("python", [Path(sys.executable)]),
        ("cmake", [Path(shutil.which("cmake") or "/nonexistent-cmake"),
                   prefix / f"cmake-{CMAKE_VERSION}-linux-x86_64/bin/cmake"]),
        ("systemc.h", [systemc_prefix / "include/systemc.h"]),
        ("SystemC package", list(systemc_prefix.rglob("SystemCLanguageConfig.cmake"))),
    ]:
        hit = next((p for p in paths if p.exists()), None)
        print(f"  {name}: {hit if hit else '缺失'}")
    import shutil as _sh
    gxx = _sh.which("g++")
    print(f"  g++: {gxx if gxx else '缺失'}")


def cmd_install(prefix: Path, systemc_version: str, only: str | None,
                install_prefix: Path | None = None):
    prefix.mkdir(parents=True, exist_ok=True)
    (prefix / "downloads").mkdir(parents=True, exist_ok=True)
    if only in (None, "cmake"):
        install_cmake(prefix)
    if only in (None, "systemc"):
        install_systemc(prefix, systemc_version, install_prefix)
    installed = (install_prefix or prefix / "systemc").resolve()
    cmd_status(prefix, installed)
    print("\n环境变量建议（加入 ~/.bashrc）：")
    cmake_bin = prefix / f"cmake-{CMAKE_VERSION}-linux-x86_64/bin"
    if (cmake_bin / "cmake").exists():
        print(f'  export PATH="{cmake_bin}:$PATH"')
    print(f'  export SYSTEMC_HOME="{installed}"')
    print(f'  export CMAKE_PREFIX_PATH="{installed}:$CMAKE_PREFIX_PATH"')


def main(argv=None):
    ap = argparse.ArgumentParser(prog="esl_env_setup", description="ESL 核心环境安装")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_install = sub.add_parser("install", help="安装 cmake/systemc")
    p_install.add_argument("--prefix", default=str(Path.home() / ".local"))
    p_install.add_argument("--systemc-version", default=SYSTEMC_VERSION)
    p_install.add_argument("--only", choices=["cmake", "systemc"], default=None)
    p_install.add_argument("--install-prefix", type=Path,
                           help="SystemC 安装目录；构建/下载仍放在 --prefix")
    p_install.set_defaults(func=cmd_install)
    p_status = sub.add_parser("status", help="查看状态")
    p_status.add_argument("--prefix", default=str(Path.home() / ".local"))
    p_status.set_defaults(func=lambda a: cmd_status(Path(a.prefix)))
    args = ap.parse_args(argv)
    if args.cmd == "install":
        args.func(Path(args.prefix), args.systemc_version, args.only, args.install_prefix)
    else:
        args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
