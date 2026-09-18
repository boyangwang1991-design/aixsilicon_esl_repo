# ESL 核心环境配方（docs/environment.md）

本机（Rocky Linux 8.10）已验证的核心环境：**Python + g++ + CMake + SystemC**。
SystemC/CMake 是并发/事件/资源竞争/目标仿真时间的核心载体（repo-plan §2）；
Python 只做 oracle/参考。

## 已验证版本组合

| 组件 | 版本 | 位置/来源 |
|---|---|---|
| Python | 3.12.13 | workflow 根 uv venv |
| g++ | 8.5.0 | 系统 |
| CMake | 4.4.3 | `/tmp/esl_cmake_venv`（PyPI wheel，`uv pip install cmake`） |
| SystemC | 2.3.4 | `~/.local/systemc`（源码编译，见下） |

## 安装步骤（用户级，无 root）

```bash
# 1. CMake（PyPI 预编译 wheel，~1s）
uv venv /tmp/esl_cmake_venv && uv pip install --python /tmp/esl_cmake_venv/bin/python cmake

# 2. SystemC 2.3.4（GitHub tag tarball 经 ghproxy 镜像下载，网络直连慢时用）
#    GitHub: https://github.com/accellera-official/systemc/archive/refs/tags/2.3.4.tar.gz
#    镜像:   https://ghproxy.net/https://github.com/accellera-official/systemc/archive/refs/tags/2.3.4.tar.gz
tar xzf systemc-2.3.4.tar.gz -C ~/.local/systemc-src
cd ~/.local/systemc-src/systemc-2.3.4
autoreconf -i                # 系统有 autoconf/automake 时生成 configure
./configure --prefix=$HOME/.local/systemc
make -j$(nproc) && make install
# 产物: ~/.local/systemc/{include/systemc.h, lib-linux64/libsystemc.a, lib-linux64/libsystemc-2.3.4.so}

# 3. 环境变量（已写入 ~/.bashrc）
export PATH="/tmp/esl_cmake_venv/bin:$PATH"
export SYSTEMC_HOME="$HOME/.local/systemc"
export LD_LIBRARY_PATH="$HOME/.local/systemc/lib-linux64:$LD_LIBRARY_PATH"
```

## 验证

```bash
# 环境检查
PYTHONPATH=<esl_repo> uv run --project . python <esl_repo>/tools/esl_cli.py doctor
# 期望全部 [OK]：python / g++ / cmake / SystemC

# 最小 SystemC 用例（CMake 全链路）
cmake -DSYSTEMC_HOME=$SYSTEMC_HOME -S examples/min_systemc -B build && cmake --build build
LD_LIBRARY_PATH=$SYSTEMC_HOME/lib-linux64 ./build/min_sc   # SystemC OK
```

## 注意事项

- **ABI 匹配**：SystemC 库按 C++14 编译（符号 `cxx201402`）；用户用例须
  `-std=c++14` 匹配，否则链接报 version symbol 错误。
- 运行时需 `LD_LIBRARY_PATH` 指向 `lib-linux64`（SystemC 装的是共享库）。
- CMake 用临时 venv（不污染子仓/不动系统）；SystemC 装 `~/.local`，两者均可复现。
