"""esl doctor：环境检查（R02）。

检查编译器、CMake、Python 与 SystemC 版本组合，报告具体缺项。
退出码：0=可运行基础（Python 资产）；2=缺关键依赖。

运行：uv run python tools/esl_doctor.py
"""
from __future__ import annotations

import shutil
import subprocess
import sys


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
    if cmake:
        print(f"[OK] cmake: {_version([cmake, '--version'])}")
    else:
        print("[BLOCKED] cmake: 未找到（纯 Python 资产不受影响）")

    # SystemC（按需扩展，基础阶段非必需）
    systemc = _which("systemc-config") or _which("systemc")
    if systemc:
        print(f"[OK] SystemC: {systemc}")
    else:
        print("[INFO] SystemC: 未找到（P10+ 扩展按需安装；基础 Python/行为资产可运行）")

    print()
    if ok:
        print("[esl doctor] 结果: 基础环境就绪（Python 资产可运行）")
        return 0
    print("[esl doctor] 结果: 缺关键依赖（见上 FAIL 项）")
    return 2


if __name__ == "__main__":
    sys.exit(main())
