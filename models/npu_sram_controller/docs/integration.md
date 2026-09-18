# 公开库接入

依赖 SystemC 3.0.2、C++17、CMake >=3.19，版本来自 repo contracts/environment.json。target 为 `aix::esl::npu_sram_controller`，package 为 `AixEslNpuSramController`。头文件 `npu_sram_controller/model.hpp` 暴露 Config、Mapper、Model、Request、Response、Metrics。

源码消费：

```bash
cmake -S models/npu_sram_controller/examples/integration -B runs/npu-consumer-source -DESL_SOURCE="$PWD"
cmake --build runs/npu-consumer-source -j4
ctest --test-dir runs/npu-consumer-source --output-on-failure
```

安装消费：

```bash
cmake -S models/npu_sram_controller -B runs/npu-library -DCMAKE_BUILD_TYPE=Release
cmake --build runs/npu-library -j4
cmake --install runs/npu-library --prefix "$PWD/runs/npu-prefix"
cmake -S models/npu_sram_controller/examples/integration -B runs/npu-consumer-install -DCMAKE_PREFIX_PATH="$PWD/runs/npu-prefix"
cmake --build runs/npu-consumer-install -j4
ctest --test-dir runs/npu-consumer-install --output-on-failure
```

这些命令在 esl_repo 根运行，SystemC 通过 SYSTEMC_HOME 指定。统一验证 CLI 自动执行源码、安装、搬迁 prefix 三种独立消费者。

Model 自带 SC_THREAD，统一 1 ns 时钟，无外部时钟绑定。驱动在周期边界调用 submit/push_w/pop，然后推进 SystemC。示例使用 `sc_start(1,SC_NS); sc_start(SC_ZERO_TIME)`，后者完成恰好位于时间边界的 delta。不能将多个周期的 CPU 调用当作额外时钟；同一端口每方向的握手限额在 Model 内检查。

submit(Request) 返回非零 token 表示接受，0 表示资源不足。读 AR/写 AW 使用相同 Request 类型并以 write 区分。写数据另调 push_w(port,data,mask,last)，最多一 beat/port/cycle；先到 W 有界缓存，需后续 AW 配对。data/mask 是 beat 内紧凑字节，mask 仅可为 0/1。Model 复制所有输入，不持有调用者 buffer 指针。

pop(port,write,response) 代表一次 RREADY/BREADY 消费。未调用即为背压；读每 beat 一个响应，last 对应 burst 结束，写每 burst 一个完成。返回的数据值在 Bank issue 取样，不是消费时读取全局数组。不同实例存储、计数和事件互相独立。

stop_scrub 后继续推进并消费响应直到 idle；任何已接纳读写/内部请求不能省略。reset 在 idle 时清空本实例（含存储/统计），在途调用抛出 logic_error。异常配置/非法输入抛出 invalid_argument/out_of_range；ECC 不可纠错通过 response.error 和 poison 表达，调用者负责传播任务失败。

set_observer 接受只读事件回调，不能重入/修改模型。开启/关闭 observer 应得到相同完成时刻。生命周期、数据/时间所有权与不支持项详见 design.md。
