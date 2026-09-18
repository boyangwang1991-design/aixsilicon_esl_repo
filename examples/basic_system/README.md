# basic_system：B0 可运行基础系统

状态唯一见 registry 的 `aixsilicon:esl:basic_system:0.1.0`。本顶层交付 B0；B1 外设由 [interrupt_system](../interrupt_system/README.md) 和 [peripheral_system](../peripheral_system/README.md) 验证；B2 命令式 DMA 由 [dma_system](../dma_system/README.md) 验证。B3–B4 仍是规划。当前使用 C++ Config 显式装配，不宣称具备通用 system.yaml 构建器。请用以下 CMake 入口，Python mini_pipeline 的 run 后端不能执行此系统。

## 目标组成

最小闭环：host_master → tlm_bus → ram/rom。外设闭环加入 timer、irq_controller、uart、gpio。数据搬运闭环加入 dma；算法系统按需加入 compute/bmu，详细性能探索再加入 banked_sram。

```text
host_master.mem ─┐
dma.mem ────────┼─> tlm_bus ─> rom / ram / 外设 MMIO
compute.mem ────┘
timer/uart/gpio/dma/compute irq ─> irq_controller ─> testbench 观察
上层 testbench ─> clock/reset、输入激励、停止/超时、最终 scoreboard
```

后续 CPU/ISS 可通过 TLM adapter 替换 host_master；当前 host_master 只执行顶层 SC_THREAD 的 read/write 调用，不能证明软件启动。bmu 属于加速器服务，通过有类型接口共享，不伪装成必须 MMIO 的外设。

## 拟定基础连接与地址

socket 数据宽度 32 bit、TLM 地址为 64 bit 字节地址；本例映射位于 32 bit 地址范围，字节透传；初版 LT/full_data。时间分辨率由顶层设置，初始参考 1 ns；latency 作为明确参数，尚不声明实测性能。总线将系统地址转成 target 局部地址。

| 目标 | 基址 | 窗口大小 | 访问/用途 |
|---|---|---|---|
| rom | 0x00000000 | 64 KiB | 只读初始化映像 |
| ram | 0x10000000 | 1 MiB | 主机、DMA、Compute 共享同一存储 |
| timer | 0x20000000 | 4 KiB | 探索 CSR |
| irq_controller | 0x20001000 | 4 KiB | 探索 CSR |
| uart | 0x20002000 | 4 KiB | 探索 CSR |
| gpio | 0x20003000 | 4 KiB | 探索 CSR |
| dma | 0x20004000 | 4 KiB | CSR 前端实现后启用；初版命令注入 |
| compute | 0x20005000 | 4 KiB | 可选，CSR 前端实现后启用 |

完整系统 IRQ 输入位规划：timer=0、uart=1、gpio=2、dma=3、compute=4；未装配源绑定未触发电平。各 B1 消费者的实际编号见自身 README。基础系统只需要前四类外设，算法扩展不是最小系统前置条件。B0 映射目前由 systemc/main.cpp 的 Config 实现；完整系统装配仍待实施，硬件兼容需要新明确配置而非悄悄修改示例 ABI。

## 分阶段可执行验收

| 阶段 | 可观察结果 |
|---|---|
| B0 主机/总线/RAM/ROM | 已知图样读回；ROM 写失败；未映射地址返回错误；事务有界终结 |
| B1 中断/外设 | timer IRQ 经控制器被主机观察；mask/clear 后状态正确；UART loopback；GPIO 输入触发 |
| B2 DMA/双主设备 | 主机预填 RAM，DMA copy 与主机访问竞争，无丢失；完成时数据可见；访问错误和 reset 清理 |
| B3 可选算法扩展 | DMA/Compute/BMU 串接，独立算法参考检查；有限 slot 背压和排空 |
| B4 可选 bank 扩展 | 依据专项 M0–M8 验证，不能把基础 RAM 数据当 bank 性能证据 |

实现时交付独立 CMake 顶层、实例/端口配置、工作负载与 scoreboard；既支持源码树也支持安装包消费模型，不直接 include 模型私有源码。测试正常、错误、两个实例及 reset/drain 后系统继续执行。已接受请求恰好一次终结，失败不靠 sc_stop 吞掉未完成事务。

停止条件：预定脚本完成、所有模型 drain/idle、scoreboard 完成；另设目标时间和宿主超时。基础系统不引入 CPU/OS、cache coherency、DDR/NoC 详细模型或 CDC。

## B0 构建与复现

从资产仓库根执行，先设置 SYSTEMC_HOME 指向已安装的 3.0.2（C++17）。

```bash
cmake -S examples/basic_system -B build/basic-source -DESL_MODELS_SOURCE_DIR="$PWD"
cmake --build build/basic-source -j 4
ctest --test-dir build/basic-source --output-on-failure

cmake -S . -B build/models -DESL_ENABLE_SYSTEMC=ON -DCMAKE_INSTALL_PREFIX="$PWD/build/prefix"
cmake --build build/models -j 4
cmake --install build/models
cmake -S examples/basic_system -B build/basic-installed -DCMAKE_PREFIX_PATH="$PWD/build/prefix"
cmake --build build/basic-installed -j 4
ctest --test-dir build/basic-installed --output-on-failure
```

自动执行 RAM 专项、上述两种消费者及 prefix 移动后重新构建：
`uv run python tools/validate_basic_models.py --cmake /path/to/cmake`。
在 workflow 中使用根 uv 环境运行该脚本的完整路径。输出到新 runs 目录，不覆盖历史结果。

B0 实际组件为两个 host、一条串行总线、一个 RAM 和一个 ROM。测试具有 1 us 仿真上限与 15 s 宿主超时。当前总线不提供公平仲裁承诺；host/bus 不支持热 reset，须先 drain。RAM/ROM 提供 reset。B1/B2 在独立消费者交付，不包含在本 B0 顶层中；B3–B4 仍待实现。结果见 [验证记录](../../docs/basic_models_validation.md)。
