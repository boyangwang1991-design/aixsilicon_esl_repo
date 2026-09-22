# 安装与接入

SystemC 3.0.2/C++17，由同一环境合同约束；顶层设置时间分辨率及 sc_main。源码接入 `add_subdirectory(${ESL_MODELS_SOURCE_DIR}/common common)`，安装接入 `find_package(AixEslCommon 0.1 CONFIG REQUIRED)`，两者均链接 `aix::esl::common`。不需要第二套 SystemC runtime，不设置全局 CMAKE_CXX_FLAGS。

包含 `<aix/esl/bounded_queue.hpp>`、`<aix/esl/blocking_gate.hpp>`、`<aix/esl/byte_store.hpp>` 或 `<aix/esl/activity_monitor.hpp>`。公开头文件的完整清单见 common.yaml；源码树能找到的 Mmio32 不因此成为公共接口。每个容器/闸门实例自行持有状态，必须由 owner 保证存活时间和同内核调用。

从 ESL Repo 根执行（SYSTEMC_HOME 指向安装）：

```bash
cmake -S examples/common_primitives -B build/common-source -DESL_MODELS_SOURCE_DIR="$PWD"
cmake --build build/common-source -j 4
ctest --test-dir build/common-source --output-on-failure
cmake -S common -B build/common-package -DCMAKE_INSTALL_PREFIX="$PWD/build/common-prefix"
cmake --build build/common-package
cmake --install build/common-package
cmake -S examples/common_primitives -B build/common-installed -DCMAKE_PREFIX_PATH="$PWD/build/common-prefix"
cmake --build build/common-installed -j 4
ctest --test-dir build/common-installed --output-on-failure
```

移动 prefix 后必须用新的 build 目录重新 configure；统一 tools/validate_basic_models.py 执行源码/安装/搬迁及真实模型回归。此公共包可单独安装；模型内部使用相同 canonical 头文件，不要求模型用户在代码里操作队列/租约。源码 include 路径不应出现在导出 target 中。

等待 queue.changed_event 或 gate.idle_event 前先检查谓词，delta 通知可能合并。业务 reset 不能依赖 clear 自动产生完成记录；BlockingLease 构造本身申请 credit，不能再手动 enter。具体数据/时间/容量边界见 [合同](../../contracts/common_primitives.md)。

## 源码分层与 manifest v2

common.yaml 的 schema_version=2，headers 改为相对仓库根的唯一实现路径；
schema_version=1 的旧相对 common 路径仍由校验器识别。资产 ID/公开 include/target/package
均不变，安装后仍为 include/aix/esl/*.hpp。直接硬编码旧 common/systemc/include 的消费者
应改为链接 aix::esl::common；不提供旧实现副本或链接别名。内部 Mmio32 位于 services，仍不安装。

包本身没有 Python 构建依赖；源码方式通过聚合 target 获得各层 include 路径，安装方式只获得
安装 prefix 的 include 路径。安装验证对比实际导出头文件与 manifest 的完整集合，并检查源码路径泄漏。
