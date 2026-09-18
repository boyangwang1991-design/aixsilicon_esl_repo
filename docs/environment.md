# ESL 目标环境配方

版本唯一配置在 [contracts/environment.json](../contracts/environment.json)：当前锁定 **SystemC 3.0.2、C++17**，包含 TLM。2026-09-18 核对 [Accellera 官方下载页](https://www.accellera.org/downloads/standards/systemc)，它是最新的 3.0 系列正式版本。升级时修改唯一配置并重新验收，不追踪浮动 main。

[官方 3.0.2 安装说明](https://github.com/accellera-official/systemc/blob/3.0.2/INSTALL.md) 推荐 CMake，列出 GCC >=9.3 或 Clang >=13，C++17 基线。本仓消费者使用 CMake >=3.19，统一通过 SystemCLanguage package 和 SystemC::systemc target 连接。编译器/标准库/编译选项必须与库一致。

## 当前证据

2026-09-18 已安装至 `~/.local/systemc-3.0.2`，保留原 `~/.local/systemc`（2.3.4）。`.bashrc` 的 ESL 块已切换到新目录并备份。新 shell 的 doctor、SystemC 时钟计数与 TLM 读写/越界/目标时间检查通过；ldd 确認运行时加载新库。证据见 [本次检查](../runs/systemc-3.0.2-install/checks.json)。

实测组合为 GCC 8.5.0、C++17、CMake 4.4.3。官方建议 GCC >=9.3；当前组合已完成本机源码编译及上述冒烟验证，但不是全模型或全部编译器特性的兼容性认证。模型和标准集成 I01–I07 仍需各自验收。

CMake package 实际版本为 `3.0.2.20251031`，末尾是发布日期。Repo 构建入口检查前三段发行版本，避免 `EXACT 3.0.2` 错拒官方包，同时拒绝其他补丁版本。

## 安装与验证入口

从 workflow 根复用现有 uv 环境；不创建第二个 venv。准备官方源包并核实来源，使用独立安装前缀，不覆盖旧环境。安装脚本从唯一配置读取默认版本，使用 CMake 构建；执行安装须属于用户当前任务范围。

```bash
uv run --no-sync python repos/aixsilicon_esl_repo/tools/esl_env_setup.py install --only systemc --prefix cache/esl-env-3.0.2 --install-prefix "$HOME/.local/systemc-3.0.2"
```

该入口需要可用的 CMake、兼容编译器与网络/缓存；显式 install-prefix 作为 CMAKE_PREFIX_PATH（未指定时为 prefix/systemc）。源码包及工具版本、来源和实际 hash 留在构建证据；不要把缓存存在当作下载来源已验证。

```bash
cmake -S repos/aixsilicon_esl_repo/tests/environment/systemc -B build/esl-min-systemc3 -DCMAKE_PREFIX_PATH="$HOME/.local/systemc-3.0.2"
cmake --build build/esl-min-systemc3
ctest --test-dir build/esl-min-systemc3 --output-on-failure
```

SystemC 3 的 CMake 安装可能使用 lib/lib64；通过 package 解析依赖，不硬编码旧 lib-linux64。仅有头文件或找到旧 systemc-config 不算 3.0.2 就绪。真实目标库缺失时 SystemC 构建应失败；Python 算法参考可独立运行，但不是目标交付完成。

当前 shell 可用 `source ~/.bashrc` 载入新配置；新 shell 自动读取。CMake 沿用机器已有安装路径，未另建 Python 环境。
