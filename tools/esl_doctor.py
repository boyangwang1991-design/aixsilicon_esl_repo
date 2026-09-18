"""esl doctor：环境检查（R02）。

检查编译器、CMake、Python 与 SystemC 版本组合，报告具体缺项。
退出码：0=目标环境静态检查满足；2=缺关键依赖/版本不符。
静态检查不代替实际 SystemC 编译链接与运行。

运行：uv run python tools/esl_doctor.py
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ENVIRONMENT = json.loads((Path(__file__).resolve().parents[1] / "contracts/environment.json").read_text())


def check_systemc(prefix):
    """Check the selected installation, including version and CMake consumption."""
    prefix = Path(prefix)
    header = prefix / "include/sysc/kernel/sc_ver.h"
    if not header.is_file():
        return False, f"缺版本头文件: {header}"
    text = header.read_text(encoding="utf-8")
    components = [re.search(rf"^#define\s+SC_VERSION_{part}\s+(\d+)\b", text, re.M)
                  for part in ("MAJOR", "MINOR", "PATCH")]
    if not all(components):
        return False, f"无法解析 SystemC 版本: {header}"
    version = ".".join(item.group(1) for item in components)
    expected = ENVIRONMENT["systemc_version"]
    if version != expected:
        return False, f"{prefix}: 实际 {version}，目标 {expected}"
    libraries = [p for pattern in ("lib*/libsystemc.a", "lib*/libsystemc.so*",
                                   "lib/*/libsystemc.so*") for p in prefix.glob(pattern)]
    if not any(p.is_file() for p in libraries):
        return False, f"{prefix}: {version} 头文件存在，但缺库文件"
    packages = list(prefix.rglob("SystemCLanguageConfig.cmake"))
    if not packages:
        return False, f"{prefix}: 缺 SystemCLanguage CMake package"
    return True, f"{prefix}: {version}，头文件/库/package 完整（仍需实际构建运行验证）"


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
        ok = False

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
        ok = False

    # SystemC（核心目标时间载体；检查 ~/.local/systemc 与 PATH）
    systemc_home = os.environ.get("SYSTEMC_HOME", f"{home}/.local/systemc")
    systemc_ok, detail = check_systemc(systemc_home)
    print(f"[{'OK' if systemc_ok else 'BLOCKED'}] SystemC: {detail}")
    ok = ok and systemc_ok

    print()
    if ok:
        print("[esl doctor] 结果: 目标环境静态检查满足；模型须另行构建/运行验证")
        return 0
    print("[esl doctor] 结果: 缺关键依赖（见上 FAIL 项）")
    return 2


if __name__ == "__main__":
    sys.exit(main())
