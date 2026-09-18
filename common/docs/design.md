# 设计边界与复用依据

契约唯一见 [公共合同](../../contracts/common_primitives.md)。采用独立的小型组合组件，没有统一 ModelBase、全局事件总线或通用调度框架。

Queue 在 DMA/UART 中处理相同的有限 FIFO 约束；业务层继续决定取出时机、跨队列 credit 和帧/命令完成。Gate 在 host/bus/MMIO 中处理相同的在途计数与排空，Lease 提供异常路径自动释放。ByteStore 将 RAM 的字节使能复制集中到存储层，ROM 直接继承同一实现。Monitor 被 Queue/Gate 两类资源共同使用，避免每个模型重复维护占用积分。

不把 UART 错误中断、DMA reset epoch、RAM 的服务时间或总线互斥搬入 common；这些语义当前不同，强行共享会让接口更复杂。公共资源保留被动预留接口；DAG、链路和事件记录由独立 fixture 与组合系统验证，不替代 SystemC 内核。

所有组件是 header-only，公共 ABI 为源码级 C++17 API，安装只导出 common.yaml 列出的头文件。模型自身公共 API 不暴露 common 类型，因此本轮是私有实现复用，不改变各模型对外 Config/端口。活动请求/存储所有权仍在具体模型，组件不保留用户 TLM payload。
