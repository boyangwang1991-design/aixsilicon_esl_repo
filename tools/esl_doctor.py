"""esl doctor：环境检查（R02）。

检查编译器、CMake、Python 与 SystemC 版本组合，报告具体缺项。
退出码：0=可运行基础（Python 资产）；2=缺关键依赖。

运行：uv run python tools/esl_doctor.py
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


def _which(name):
    return shutil.which(name)


def _version(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        return (out.stdout or out.stderr).strip().splitlines()[0] if (out.stdout or out.stderr) else "unknown"
    except Exception as exc:  # noqa: BLE001
        return f"error: {exc}"


def check_python():
    return sys.version_info, f"{sys.version.split()[0]}"


def main():
    ok = True
    print("[esl doctor] 环境检查\n")

    # Python（基础资产必需）
    py_major, py_minor = sys.version_info[:2]
    if (py_major, py_minor) >= (3, 9):
        print(f"[OK] python: {sys.version.split()[0]} (>=3.9)")
    else:
        print(f"[FAIL] python: {sys.version.split()[0]}（需要 >=3.9）")
        ok = False

    # 编译器 / CMake（C++/SystemC 资产按需）
    gxx = _which("g++") or _which("clang++")
    if gxx:
        print(f"[OK] C++ compiler: {gxx} {_version([gxx, '--version'])}")
    else:
        print("[BLOCKED] C++ compiler: 未找到 g++/clang++（纯 Python 资产不受影响）")

    cmake = _which("cmake")
    home = str(Path.home())
    candidates = [f"{home}/.local/bin/cmake", "/tmp/esl_cmake_venv/bin/cmake"]
    import glob as _glob
    candidates += _glob.glob(f"{home}/.local/cmake-*/bin/cmake")
    if cmake is None:
        for c in candidates:
            if Path(c).exists():
                cmake = c
                break
    if cmake:
        print(f"[OK] cmake: {cmake} {_version([cmake, '--version'])}")
    else:
        print("[BLOCKED] cmake: 未找到（SystemC 模型构建需要；见 esl_env_setup.py）")

    # SystemC（核心目标时间载体；检查 ~/.local/systemc 与 PATH）
    systemc_home = os.environ.get("SYSTEMC_HOME", f"{home}/.local/systemc")
    sysc_header = Path(systemc_home) / "include" / "systemc.h"
    sysc_lib = Path(systemc_home) / "lib-linux64" / "libsystemc.a"
    if sysc_header.exists() and sysc_lib.exists():
        print(f"[OK] SystemC: {systemc_home} (libsystemc.a)")
    elif _which("systemc-config"):
        print(f"[OK] SystemC: {_which('systemc-config')}")
    else:
        print("[BLOCKED] SystemC: 未找到（核心并发/事件/资源模型需要；见 esl_env_setup.py）")

    print()
    if ok:
        print("[esl doctor] 结果: 基础环境就绪（Python 资产可运行）")
        return 0
    print("[esl doctor] 结果: 缺关键依赖（见上 FAIL 项）")
    return 2


if __name__ == "__main__":
    sys.exit(main())
