# 公开接入

依赖 SystemC 3.0.2、C++17、CMake >=3.19。库目标 `aix::esl::npu_mesh`，
包 `AixEslNpuMesh`，头文件 `<npu_mesh/model.hpp>`、`<npu_mesh/config.hpp>`、`<npu_mesh/axi.hpp>`。
调用者拥有 sc_start/sc_stop；库不会终止全局仿真。模型使用有类型 command API，未提供 TLM socket/DMI。

```bash
cmake -S models/npu_mesh/examples/integration -B /tmp/mesh-source -DMESH_SOURCE="$PWD/models/npu_mesh" -DCMAKE_PREFIX_PATH="$SYSTEMC_HOME"
cmake --build /tmp/mesh-source
/tmp/mesh-source/consumer
cmake --install /tmp/mesh-source/mesh-library --prefix /tmp/mesh-prefix
cmake -S models/npu_mesh/examples/integration -B /tmp/mesh-installed -DCMAKE_PREFIX_PATH="/tmp/mesh-prefix;$SYSTEMC_HOME"
cmake --build /tmp/mesh-installed
/tmp/mesh-installed/consumer
```

以上在 ESL repo 根执行。CLI validate 自动执行源码消费、安装及移动 prefix 后消费。
Config::endpoints 必须非空，声明全局 base/size、router 坐标索引、服务容量与速率。
命令接口无必绑 socket；配置无效构造抛异常。submit 返回 OK+handle 或 RETRY/错误+0，
take(handle) 消费唯一完成；等待可用 completion_event。数据为值语义，不保留调用者指针。

AxiNiu 的 aw/w/ar 返回 true 表示一次握手，false 表示调用方保留消息后重试；
b/r 弹出响应。W-before-AW 被反压；不表示引脚 ready/valid 时序已验。错误合法 burst
排空 W 后返回一个 B，读错误返回规定拍数并清零数据。Status::decode 对应 DECERR，
其他异常对应 SLVERR，成功 OKAY；永不产生 EXOKAY。beat_bytes 是 2 的幂且不大于 bus_bytes，
beats 1..256；4KB 违法、非 INCR/exclusive/Device 被诊断拒绝。无独立 AXI frontend reset API，
上层应先停止 frontend 并排空，然后复位 Model；不声明在途 frontend reset 支持。

目标 Tile 需 reserve(context,address,length) 后使用 token。release 在 pin/在途引用时拒绝。
initialize/inspect 仅在 idle 使用，是不收费的调试接口，不能用作模拟流量。
管理 direct API 在数据拥塞期间仍可调用。reset 后取完 abort/completion 并排空，再 resume。
TensorDma 是独立 sc_module，系统判断 drain 必须同时检查 bus、dma、axi 的 idle；
bus idle 不包含尚未送入总线的外部 DMA/AXI 命令。

BM 格式见 [BM 合同](bm.md)。真实文王 Scheduler/BMU/Tile 接入应调用同一公开接口，
绑定可信 context，提供 ownership 和完成事件，不把 synthetic trace 当作实际推理接入。

新增实际存储 backend 和并发参数见 [存储接入](memory.md)。安装包传递依赖
AixEslNpuSramController；独立消费者已切换为真实 SRAM。reset/timeout 对 native 已接受写
实行 drain-before-completion，调用者不能假定 reset 返回时所有 abort 都已生成。
Completion 增加 completed_ranges；DmaCompletion 增加 bytes_per_destination、uncertain。
它们记录已知效果，不能把错误状态解释为零副作用。Request::ordered 默认true，只有可独立
执行的 DMA 子事务由 TensorDma 置false；上层不可用它绕过 AXI 同 ID 顺序约束。
