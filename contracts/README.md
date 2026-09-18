# 公共接口合同（contracts）

本目录承载少量 schema 和公共接口说明。权威定义由本 Repo 拥有（见
[`docs/esl_repo_plan.md`](../docs/esl_repo_plan.md) §2.2/§4.1）；Skill 只引用，
不另存副本。

## 历史 Python 原型接口（§4.1 最小合同）

- **请求/事件字段**：所有异步操作至少携带 `request_id / task_id / epoch` 中适用
  的字段。请求有 accepted、started、completed/failed/cancelled 三类关键边界。
- **MemoryRequest**：地址空间、地址、长度、读写、可选数据/byte enable、
  request_id/epoch。byte enable 省略表示全部有效；非空时与请求长度一致。
- **Command**：task_id、操作及类型化参数。
- **Completion**：原始 ID、epoch、状态和错误原因。
- **数据可见性**：MemoryPort 读在服务完成时取样，写在完成时可见；请求期间写
  payload 保持有效。
- **仲裁 owner**：Scheduler 判断依赖/资源资格，资源端从已接受等待集合选下一项；
  不能在两处分别重排得出不同顺序。

## profile 字段（§2.2）

| 字段 | 取值/规则 |
|---|---|
| model_kinds | behavioral / performance / software_visible / microarchitecture 集合 |
| profiles | 命名配置映射；每个 profile 显式列出能力（非简单字符串列表） |
| timing_model | untimed / annotated / resource_contention / cycle_accurate |
| data_modes | full_data / traffic_only 集合；运行时选一个 data_mode |
| interface_mode | command / register / descriptor 等项目前端名称 |
| transport | direct / LT / AT；异构端口逐端口声明 |
| capabilities | 实际支持的能力 ID（如 data_transform、shared_dma_contention） |
| limitations | 未建模因素 |

只声明已实现的组合；选择不支持的 profile/前端/数据模式必须拒绝。

当前 manifest、registry 与参考后端配置的可执行校验见 [工具合同](tool_contracts.md)。通用 SystemC 系统配置装配仍待实现，不能把历史 Python 接口视为所有 SystemC 模型的统一接口。各模型 manifest 和接口文档规定其实际数据可见性与生命周期。

基础 SystemC timer/IRQ 的实际软件可见接口见 [MMIO32 合同](mmio32.md)；它与历史 Python MemoryRequest 合同分开，不代表通用 schema 已全部实现。
