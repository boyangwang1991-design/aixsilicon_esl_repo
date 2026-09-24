# 契约差距与推进顺序

这是可运行的探索基线，不是 npu_mesh_axi_bus.md 全 P0 完成。正式 OR–DR/架构冻结、
G1/G2/G3 等均未由本轮自动授予。以下缺口不能用局部 PASS 消除：

1. **端点精度与真实流量**：已接入 npu_sram_controller，并提供 DDR 带宽/读写延迟/队列/
   切换服务模型；仍缺 DRAM command/refresh/row-buffer 模型、真实 BM 流量与 RTL 校准。
2. **依赖论证与返回资源**：生成 router+NIU+DMA+target 完整资源依赖图；本轮有隔离 VN、
   完整读返回预留、有限分片窗口和压力测试，但未形式证明事务级前进性。
3. **QoS**：已有NIU token bucket和SRAM入口优先/老化；仍无Router WRR、bank内部服务类别、全局context配额或端到端服务保证；不能把本版 Decode
   p99 宣称满足契约 1.5× 干扰目标。当前只有 RR 和独立控制网络。
4. **AXI 完整性**：无 USER/protection 的完整位段传播、Device 保持外部访问次数、FIXED/WRAP、
   同 ID 多目标 ROB、逐拍 pin 信号、frontend 在途 reset。仅验证公布的 INCR beat API。
5. **DMA/组播**：已实现大组播逐片 fanout、并发窗口及每目的字节/完成位图；仍无内存 descriptor
   fetch、group ID/version 管理、每目的 unknown 位图完整 ABI；当前 uncertain 为任务级标志。
6. **恢复/桥接**：无 CDC、异频/停钟/DVFS、单端 reset/rejoin、控制 bit 保护、物理 ECC、
   链路隔离、retention/transit power-off 状态机、epoch 有界编码回绕试验。
7. **管理/PMU**：无 CSR privilege/lock/shadow commit/IRQ/计数 snapshot/溢出 sticky、硬件有界
   trace/fault FIFO。调试接口与主机 trace 不等于这些 RTL 功能。
8. **PPA/校准**：缺 RTL/STA/布局/能耗输入；宽窄分离多用物理通道，不能当等面积获胜。
   集中基线是单共享总线，未实现分层 crossbar；本轮不推荐冻结唯一 profile。

选用参数时先看字节正确性和资源上界，再验证单链路、BDP、memory bottleneck、混合流。
增加端点、QoS、CDC 等能力后各自补行为/异常/性能自洽证据，再接真实模型推理。
