# dma_system：B2 共享内存搬运消费者

SystemC/C++17 独立工程，组合两个 host、两个不同 burst/capacity 的 DMA、一条 tlm_bus、RAM 与 ROM。模型通过公开 target 使用，不 include 私有源码。主机先初始化 RAM，DMA 分块搬运，主机读回检查真实数据；并发主机访问独立区域以检验共享链路而非隐藏副本。

地址配置：RAM 在 0x1000，大小 256 字节；ROM 在 0x2000，大小 64 字节。默认源 0x1000、目标 0x1080；DMA 使用全局字节地址。DMA0 capacity=2/burst=8，DMA1 capacity=1/burst=4；RAM 每访问 setup=1 ns，加 ceil(bytes/8)*1 ns，总线路由额外 1 ns。因此无竞争三块读写共 18 ns。另有 annotated-only target 独立测试返回 delay。

从 ESL Repo 根执行，SYSTEMC_HOME 指向已安装的 SystemC 3.0.2：

```bash
cmake -S examples/dma_system -B build/dma-source -DESL_MODELS_SOURCE_DIR="$PWD"
cmake --build build/dma-source -j 4
ctest --test-dir build/dma-source --output-on-failure

cmake -S . -B build/models -DESL_ENABLE_SYSTEMC=ON -DCMAKE_INSTALL_PREFIX="$PWD/build/prefix"
cmake --build build/models -j 4
cmake --install build/models
cmake -S examples/dma_system -B build/dma-installed -DCMAKE_PREFIX_PATH="$PWD/build/prefix"
cmake --build build/dma-installed -j 4
ctest --test-dir build/dma-installed --output-on-failure

mv build/prefix build/moved-prefix
cmake -S examples/dma_system -B build/dma-moved -DCMAKE_PREFIX_PATH="$PWD/build/moved-prefix"
cmake --build build/dma-moved -j 4
ctest --test-dir build/dma-moved --output-on-failure
```

统一自动验证由 `tools/validate_basic_models.py` 编排；Python 在这里只是构建/证据工具，所有 DMA 行为和时间断言在 systemc/main.cpp。1 us 仿真 watchdog 和 15 s CTest 超时；成功必须完成预期工作、无多余完成记录且所有模型 idle。

17 个独立用例覆盖 copy/tail、命令和完成容量、drain/reset、读/写/部分错误、非法输入、双实例、主机竞争、下游容量拒绝、annotated delay、无效配置与未绑定。逐项接口与适用边界见 [DMA](../../models/dma/README.md)；实际记录见 [验证](../../docs/basic_models_validation.md)。这不是完整 CPU/SoC 启动系统，也不实现 DMA MMIO/IRQ。
