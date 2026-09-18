# 工具与模型交付合同 v1

实际校验唯一实现在 tools/esl_contracts.py；CLI 通过它检查 available registry、model.yaml 与历史参考配置。新增字段必须同步校验器和测试，不能用注释宣称它已生效。

## 发现与校验

`esl validate` 检查当前 available 资产目录、模型 manifest/公开 API 文件、类型/范围默认值、端口方向/绑定、profile、CMake 配方，以及 available 模板完整性。planned 条目保留路径，允许历史多段 ID；available 条目要求四段 ID。该检查不静态证明 C++ 行为符合声明。

`esl inspect` 返回 available 资产及 manifest，并报告 evidence_status。`esl validate --evidence` 要求所有 available 资产的证据存在、PASS、覆盖目标路径且所有记录的 SHA-256 与当前文件一致。runs 是本地制品；在没有制品的独立 checkout 中证据为 UNAVAILABLE，不能沿用 PASS，需重跑工具并更新 registry.evidence。CURRENT 只说明对应 run 有效，不等于未测试的 I 项通过。

## 明确执行后端

### 历史 ID 兼容

registry 的 aliases 是旧五段 ID 的唯一发现映射；`esl inspect --id <旧或新 ID>` 返回 resolved_id（规范四段 ID），所有规范 ID/别名全局唯一，歧义或非法别名拒绝。DMA 使用 aixsilicon:esl:dma:0.1.0，保留旧 aixsilicon:esl:dma:command:0.1.0 作为发现别名。别名不选择执行后端、不改写历史 Python 配置，不能据此把 legacy_reference 的结果认作 SystemC 验证。尚未实现的其他五段 ID 在各资产首次实现时按此规则迁移。

当前 run/sweep 只接受显式 `legacy-python-mini-pipeline`（配置 backend 或 --backend）。固定系统 ID、dma/bmu/compute 引用、连接和参数所有权均校验；不接受其他系统或 SystemC 后端，不回落到参考实现。SystemC 使用 examples 下的 CMake 消费者和 tools/validate_basic_models.py。

历史 model ID 在这个后端中仅指 reference/legacy_python 实现，不把 registry 中 planned 的 SystemC DMA/Compute 升级可用。结果标记 scope=legacy_reference、target_validation=NOT_RUN。n_elements 必须为 256 的倍数且不超过 32768，num_slots 为 1..8；未知或放错实例的参数拒绝。

数据判据直接计算独立 int16 饱和结果，不调用 DUT ComputeShell；时间判据验证总时间在共享 DMA/Compute 服务下界与完全串行上界之间。它是参考模型的基本自洽检查，不代表硬件校准或所有性能不变量都已覆盖。

每次 run 用唯一目录保存 system.yaml 和 result.json。比较两个 result.json 要求成功、相同后端/profile/时间单位/工作量和有效指标；旧的空 JSON 或不同数据口径拒绝。sweep 默认笛卡尔积，zip 必须显式选择且列表等长；所有失败点保留，任一点失败则整体非零退出。所有 FAIL/BLOCKED 退出码非零；CLI 参数错误使用 argparse 非零返回。

## 标准库模板

`esl new model demo --template register_target --output /new/path` 只接受 available 模板。先预检完整文件/安全路径/类型，再在临时目录渲染并校验，最后移动到目标；不覆盖已有路径、不静默漏文件。输出是未登记资产，后续修改需独立验收。

模板产物支持 standalone/source/install/relocated 消费，拥有公开 Model/Config、manifest 和三份文档。模板的共享 CMake 支撑由生成器复制当前 Repo 权威文件，记录在模板验证 hash 中；不在模板源中维护第二套打包代码。旧 compute_cpp 和 Python 模板保持不可生成，历史文件仅供迁移参考。

安装文档保持源树相对布局于 share/aix-esl；包含公共 contracts、模型及导航文档。文档中本地 runs 的历史证据不随包发布，需另行保留制品。独立部署仍必须提供兼容 SystemC runtime。
