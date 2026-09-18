# tlm_bus 设计

标准 TLM-2 LT 地址译码互连，多输入到多输出；regions 配置全局 base/size 和输出绑定序号 target。拒绝空表、零长度、地址溢出与重叠；elaboration 检查每个 target 已绑定。整个 payload 必须落在单一窗口；未命中/跨窗口 ADDRESS_ERROR。调用下游时地址减 base，返回时恢复原全局地址。

max_outstanding 包括等待与服务；满或 drain 立即 GENERIC_ERROR。先消费入站 delay，再取得单服务 mutex，收费 route_latency，阻塞转发到下游，消费剩余 delay。锁覆盖下游服务，因而所有目标共享串行总线。排序按 SystemC mutex 调度，不承诺 round-robin 公平性。没有 reset/cancel，先 drain 再复位目标。
