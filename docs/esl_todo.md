---
document_type: implementation-todo-and-acceptance-plan
name: esl-todo-and-acceptance
version: 1.2.0
status: planned
date: 2026-09-17
references:
  - esl-repo-plan.md
  - esl-development-suite-plan.md
  - esl-testcase.md
---

# ESL Repo / Skill：TODO LIST 与验收计划

目标：先交付一套可以复用组件、运行小系统、观察性能并比较架构的轻量工具链，再按需要扩展寄存器、descriptor 与精细内存模型。

**Repo 放资产，Skill 放方法。** 本文是待执行清单，不代表代码已开发或测试已通过。所有任务初始未完成；实际推进时勾选，并链接已有测试输出，不另建多层 Gate、RTM 或重复报告。

配套依据：[Repo 规划](esl-repo-plan.md)、[Skill 规划](esl-development-suite-plan.md)、[Mini Pipeline 测试规划](esl-testcase.md)。

## 1. 怎么使用这份清单

- **基础必做：**完成后即可用于架构探索，必须通过基础验收。
- **扩展按需：**不阻塞基础交付；一旦宣称支持，就必须通过对应扩展验收。
- **后续储备：**无明确使用场景时不开发。
- 任务完成条件是“产物存在且对应检查通过”，不是“文档中写了支持”。
- 研发任务用未勾选/已勾选管理；测试用 PASS / FAIL / BLOCKED / NOT_RUN / NOT_APPLICABLE。
- 基础必测项不得随意标 NOT_APPLICABLE；缺环境标 BLOCKED，没运行标 NOT_RUN。

不预设人员姓名与工期。可以把表格导入现有项目管理工具，补 owner；验收记录优先引用工具自动生成的 run/checks。

## 2. 资产与方法的交付边界

| 类别 | 交付位置 | 内容 |
|---|---|---|
| 公共组件、模型 | esl_repo/common、models | Queue、Resource、Memory、BMU、DMA、Compute、Scheduler |
| 模板与生成器 | esl_repo/templates、tools | 模型/系统/实验/报告模板，确定性生成工具 |
| 性能观察与测试 | esl_repo/common、tools | Counter、Trace、微基准、oracle、sweep、比较器 |
| 环境与示例 | esl_repo | 环境配方、构建入口、mini_pipeline、测试输入 |
| 工作方法 | 主入口及八个subskill各自的 SKILL.md / references | 问题定义、选型、建模深度、实验组织、调试和解释 |
| 实验输出 | runs 或工程制品路径 | 配置、指标、检查结果、摘要、可选 trace |

不用 Skill，也必须能通过 Repo CLI 运行示例。Skill 可以修改 Repo，但生成的组件、模板和脚本仍归 Repo，不复制到 Skill 包。

## 3. Repo 基础 TODO

### A. 环境与最小接口

| 完成 | ID | TODO | 完成条件 |
|---|---|---|---|
| [ ] | R01 | 建立仓库目录、CMake/Python 入口、最小 registry | 示例依赖能解析；不要求独立资产服务器 |
| [ ] | R02 | 固定一套 Linux 环境配方，实现 doctor | 干净环境可配置构建；缺依赖能指出具体缺项 |
| [ ] | R03 | 定义精简 model/system/experiment 配置格式 | 统一model_kinds与profiles映射；参数、能力组合、未知字段和非法连接可检查 |
| [ ] | R04 | 实现配置解析与模型 Factory | 输出最终生效值；未知模型/端口拒绝，参数不静默丢失 |
| [ ] | R05 | 固定 Command/Memory/Event 最小合同 | 明确接受/重试/终结、地址空间、byte enable、取样/可见性、payload与仲裁owner |
| [ ] | R06 | 提供实例 ID、整数 tick、epoch 与基础生命周期 | 同刻处理顺序明确；能够 reset、drain、终止 |

R03/R05 只需要短说明和可执行校验，不要求完整规范书。实际字段、错误码和工具接口由 Repo 维护，Skill 只引用。

### B. 公共行为与资源组件

| 完成 | ID | TODO | 完成条件 |
|---|---|---|---|
| [ ] | R07 | BoundedQueue | 深度只计等待项；满队列 retry 不丢失/重复请求 |
| [ ] | R08 | 非流水 ServiceResource | 有限容量、排队、服务起止与释放；单 server 不重叠 |
| [ ] | R09 | 基础仲裁策略 | FIFO 与确定性选择；支持示例的 ready store 优先规则 |
| [ ] | R10 | ByteStore + MemoryPort | 真实数据、地址边界、byte enable、完成时可见 |
| [ ] | R11 | BufferPool | 原子 slot 分配、满池等待、generation、重复释放检测 |
| [ ] | R12 | Event/TaskGraph | 依赖状态检查、唤醒、终结唯一性、上游失败传播 |
| [ ] | R13 | 公共排空/取消支持 | 活动访问结束前不复用 buffer；旧 epoch 不污染新任务 |

每个组件只写少量能够区分对错的单测。优先测容量边界、时间顺序和异步清理，不为 getter/setter 堆测试。

### C. 基础模型

| 完成 | ID | TODO | 完成条件 |
|---|---|---|---|
| [ ] | R14 | 命令式 DMA | 真实copy；行为核心可在functional与resource profile复用；load/store共享资源 |
| [ ] | R15 | ComputeShell + vector_affine | int32中间值、int16饱和；functional与resource复用核心，不强求纯功能模型依赖SystemC |
| [ ] | R16 | BMU 模型 | 包装 BufferPool；占用、等待、释放可观察 |
| [ ] | R17 | 简单 Scheduler + Host | 提交 load→compute→store DAG；不模拟 CPU 指令 |
| [ ] | R18 | mini_pipeline 组合资产 | 地址/slot/任务关系在配置中；不复制公共组件源码 |

### D. 性能观察与测试资产

| 完成 | ID | TODO | 完成条件 |
|---|---|---|---|
| [ ] | R19 | Counter/Gauge/Span/Event 公共 API | 目标时间统一；关联 ID 放 trace，避免指标标签膨胀 |
| [ ] | R20 | Queue/Resource/Memory/Buffer 自动埋点 | 模型不重复实现 busy、queue 积分和容量统计 |
| [ ] | R21 | 指标聚合器 | 正确计算计数、busy、占用、吞吐与overlap；whole_run含最后完成，零窗口不除零 |
| [ ] | R22 | Trace 与时间线导出 | 任务可关联；tick 单位换算正确；并发轨道合法 |
| [ ] | R23 | 观察开关与失败环形缓冲 | off/counters/trace 不改变目标行为；截断有标记 |
| [ ] | R24 | ScenarioRunner + 独立 oracle | 可定位首个数据差异；不调用被测功能当参考 |
| [ ] | R25 | 确定性错误/取消/复位注入 | 可指定 chunk/事件边界；失败可重放 |
| [ ] | R26 | 性能自洽检查 | 检查字节守恒、容量、资源下界与完成先后 |
| [ ] | R27 | SweepRunner + RunComparator | 保留全部点与失败原因；比较实际配置及同口径指标 |
| [ ] | R28 | 自动结果与简报 | run/metrics/checks/summary完整；执行状态、验收状态、外层test_status分别记录 |

### E. 模板与统一入口

| 完成 | ID | TODO | 完成条件 |
|---|---|---|---|
| [ ] | R29 | compute / command_engine / buffer_manager / memory_target 模板 | 每个生成物能独立构建并执行一个实际小案例 |
| [ ] | R30 | pipeline_system / parameter_sweep / report 模板 | 从模板创建新名称实例可运行；不依赖原示例目录名 |
| [ ] | R31 | esl inspect / new / run / sweep / compare / bench 入口 | inspect返回实际available能力；run按profile执行适用构建/检查/摘要 |
| [ ] | R32 | 环境、组件和示例 README | 说明参数、用途、限制及重跑命令；不重复维护方法体系 |

R29 不要求把 Repo 规划中的全部模板一次做完；DMA 模板可在 R14 稳定后从其薄骨架提炼，寄存器与 bank 模板归扩展项。

## 4. Skill 基础 TODO

| 完成 | ID | TODO | 完成条件 |
|---|---|---|---|
| [ ] | S01 | 主入口SKILL.md与工作类型路由 | 识别W01–W14、选择必要subskill；四步用户流程不变 |
| [ ] | S02 | 复用与模板选择方法 | 模型→参数→组合→策略/适配→新模型；从 Repo 发现能力 |
| [ ] | S03 | 建模深度选择方法 | 按behavioral/performance/software_visible/microarchitecture用途选择实际profile；不按名称猜能力 |
| [ ] | S04 | 工具调用与结果读取方法 | 依据实际能力和结构化结果；不猜工具存在或测试通过 |
| [ ] | S05 | 性能实验方法 | 固定基线、控制变量、过滤非法点、保留失败点 |
| [ ] | S06 | 观察与瓶颈解释方法 | 先指标、后局部 trace；以对照实验验证瓶颈推断 |
| [ ] | S07 | 失败与恢复方法 | 缺环境、数据错误、死锁、指标异常分别处理 |
| [ ] | S08 | 输出与限制说明方法 | 报告目标问题、结果、假设、已测与未覆盖；不要求完整 RTM |
| [ ] | S09 | 资产/方法边界检查 | Skill 包不含组件源码、模板副本、oracle、执行包装器 |

### 4.1 恢复专业subskill的开发任务

下列任务补充S01–S09，保留旧编号以免历史引用失效。八个subskill均为独立可发现入口；方法完成不等于所有后端已经实现。

| 完成 | ID | TODO | 完成条件 |
|---|---|---|---|
| [ ] | S10 | esl-model-planning | 用途/范围/精度/复用缺口/最小检查齐全；已有规格只做差异 |
| [ ] | S11 | esl-model-development | 行为、性能、软件可见、微架构四种方法；代码和模板归Repo |
| [ ] | S12 | esl-system-composition | 组件能力/端口/地址/时间owner检查；支持纯复用及补缺后返回 |
| [ ] | S13 | esl-workload-development | generator/oracle/依赖/seed/窗口/停止条件；open/closed-loop区别 |
| [ ] | S14 | esl-verification | 正常/异常/跨profile及影响范围回归；保留失败证据 |
| [ ] | S15 | esl-performance-analysis | 基线/sweep/观察/瓶颈对照；模型不足时不强行给结论 |
| [ ] | S16 | esl-model-calibration | 参考匹配、拟合/验证划分、误差与适用域；缺参考不虚报 |
| [ ] | S17 | esl-repo-maintainer | 来源/许可/依赖/兼容/登记/迁移；planned与available区别 |
| [ ] | S18 | 独立调用、交接与路由回归 | 八个入口均有方法fixture；W01–W14均有路由检查；跳过无关步骤，保留停止点 |

Skill 方法可在公共组件开发时同步编写；只有相关 Repo 工具实际可用后，才能验收“已能调用并跑通”。


## 5. 建议施工顺序

| 顺序 | 做什么 | 可见成果 |
|---|---|---|
| 1 | R01–R13；同步S01–S03及S10–S18的调用合同 | 环境、接口、有限队列/资源/存储能独立工作 |
| 2 | R14–R18，带最小 counters/oracle | 单 chunk 和单 slot 端到端运行 |
| 3 | R19–R26，补 S04/S07 | 双 slot、观察、异常与清理可验证 |
| 4 | R27–R32，补S05/S06/S08/S09并完成S10–S18方法/路由测试 | 从模板搭系统，一键运行与参数对比 |
| 5 | 执行下述基础验收 | 输出一份简短验收结果索引 |

这些是实现依赖，不是审批阶段。内部可交错开发，例如 Queue 实现时就加入 occupancy 埋点，避免事后返工。

## 6. 基础验收：只看三件事

1. **配置合法：**支持的配置能运行，非法配置及时拒绝。
2. **结果正确：**数据、依赖、完成与资源生命周期一致。
3. **性能自洽：**时间与资源约束符合模型约定，指标能够解释实验。

基础主体为 `pipeline_analytic + full_data`；另用小样本functional profile做类别/跨profile检查，不增加第二套模型核心。CPU 指令执行、真实 descriptor fetch、bank 时间与 RTL 校准均不在该范围内。不能把这些未覆盖项目写成已通过。

### 6.1 Repo 公共能力验收

| ID | 项目 | 操作与预期 | 关联 TODO |
|---|---|---|---|
| A01 | 干净环境与 CLI | 按 README 配置一次；通过 doctor，直接运行示例，无需 Skill | R01–R04、R31–R32 |
| A02 | 队列/资源 | 深度=1 持续施压；不丢/重复，等待项≤1，单 server 服务不交叠 | R07–R09 |
| A03 | 内存 | 部分 byte enable、末地址、越界访问；内容和错误符合合同 | R05、R10 |
| A04 | BMU 生命周期 | 池耗尽后等待，释放后恢复；重复释放报错；旧 handle 无效 | R11、R13、R16 |
| A05 | 事件与依赖 | 先完成后等待仍可察觉状态；失败依赖不执行后继 | R06、R12、R17 |
| A06 | 指标积分 | 合成 fixture：queue 深度在 [0,2)为0、[2,5)为2、[5,10)为1，均值=1.1，峰值=2 | R19–R21 |
| A07 | 时间与利用率 | 单 server 忙区间 [2,5)，窗口 [0,10)，busy=3、utilization=0.3 | R19–R22 |
| A08 | overlap 算法 | DMA=[0,4)、[6,10)，Compute=[2,8)，总重叠=4；不能重复累计 | R21 |
| A09 | 时间线单位 | 1 ns/tick 的1000 ticks事件导出为1 μs；关联与轨道正确 | R22 |
| A10 | trace 截断 | 限制为少量事件，显式报告丢弃；完整 counters 与未截断运行一致 | R23 |
| A11 | 生成模板 | R29/R30 每种模板各生成一个新名称资产，构建并运行最小例子 | R29–R30 |
| A12 | 结果状态 | execution正常但oracle失败时overall=FAIL；负向harness验证符合预期后test_status=PASS，原run仍FAIL | R24、R28、R31 |
| A13 | 用途/profile能力 | 实现functional/resource映射；不支持bank/traffic_only等选择拒绝；planned不可执行 | R03–R04、R31 |
| A14 | 跨profile功能 | 同一小输入的functional与pipeline_analytic数据/终态一致；前者性能检查不适用 | R14–R18、R24 |
| A15 | 窗口端点 | whole_run包含最后完成；相邻半开窗口不双计；零时长性能为null/不适用 | R21、R28 |
| A16 | 轻量行为模型 | 用纯Python行为fixture执行oracle，无需SystemC；Schema不能强制所有模型CMake/计时 | R03、R24、R31 |

A06–A09 使用短小人工可计算 fixture，不依赖 mini_pipeline 的实现来证明观察器正确。

### 6.2 Mini Pipeline 基础验收

完全沿用配套规划的 T01–T12；此处给执行清单，不重新定义模型语义。

| 完成 | 用例 | 通过条件 |
|---|---|---|
| [ ] | T01：单完整 chunk/单 slot | 256元素、输出逐项一致；76 ticks |
| [ ] | T02：64 chunk/单 slot | 16,384元素全部一致；4,864 ticks；DMA/Compute 不重叠 |
| [ ] | T03：64 chunk/双 slot | 输出一致；2,592 ticks；存在正确的搬运/计算重叠 |
| [ ] | T04：DMA queue=1 | slot=1和2均不丢/重复任务，无容量越界；不强制沿用默认队列的周期 |
| [ ] | T05：尾块 | N=1/255/257 的数据与 guard 区均正确 |
| [ ] | T06：非法配置 | slot超容量、queue=0在仿真前拒绝，指出具体字段 |
| [ ] | T07：load失败 | 后继compute/store不执行；按fail-fast规则终结并排空 |
| [ ] | T08：store失败 | 不计成功；活动访问排空后释放slot |
| [ ] | T09：取消/复位 | 分别覆盖DMA和Compute活动期间；无旧epoch污染、悬挂引用或提前复用 |
| [ ] | T10：观察档位 | off/counters/trace下的数据与目标仿真时间一致 |
| [ ] | T11：重跑 | 相同配置/seed目标数据、调度、指标一致，wall time和run_id可不同 |
| [ ] | T12：故意错误的oracle | 被测run报告FAIL并定位首个差异；外层负向测试符合预期才PASS |

精确周期只适用于配套文档的聚合时间、默认数据规模、零时间控制、默认调度策略与队列设置。它们是解析测试预期，不是芯片指标，不可套用于 memory_resource。

以下统计要求适用于counters/trace档；off只检查功能、终态和目标时刻，性能采集记NOT_RUN。默认64 chunk成功运行还必须同时满足：

- 完成64个chunk，load/compute/store各64次，每个任务只完成一次。
- 外存读32 KiB、写32 KiB；SRAM总逻辑访问128 KiB；输出有效字节32 KiB。
- DMA累计busy=2,560 ticks，Compute累计busy=2,304 ticks。
- 结束时slot占用为0，活动请求为0；峰值不超过配置slot数量。
- 每个chunk均满足load完成≤compute开始、compute完成≤store开始、store完成≤slot释放。
- 单server上无服务区间交叠，成功完成不早于数据可见。

### 6.3 参数探索与报告验收

执行配套 E0–E6 共7个点：slot=1/2/4，双slot下改变DMA带宽及Compute lane数。禁止静默缩小参数范围或丢弃失败点。

| ID | 判据 |
|---|---|
| P01 | 每个点保存实际生效配置、数据模式、profile、运行/正确性状态 |
| P02 | E0/E1满足上述解析预期；其他点数据正确并满足各自资源下界 |
| P03 | 比较表包含总ticks、samples/tick、DMA/Compute利用率、BMU峰值和speedup |
| P04 | 同口径成功运行才计算speedup；失败/超时点明确展示，不能填0耗时 |
| P05 | 至少用一段时间线解释双缓冲减少了什么等待、为何不是无限加速 |
| P06 | 报告区分“资源模型内的结果”和真实芯片预测；说明未建模bank/descriptor/RTL细节 |
| P07 | 比较不同workload/profile/统计窗口时给出差异，不能伪装成只改一个参数 |

不硬性要求每个优化都改善性能。没有收益甚至变差，只要结果正确、调度明确且解释有证据，同样可以通过探索方法验收。

## 7. Skill 方法验收

在已有 Repo 资产上执行以下任务。评分看实际行为和产物，不要求回答措辞完全一致。

| ID | 给 Skill 的测试请求 | 通过条件 |
|---|---|---|
| S-A01 | “用已有组件搭mini_pipeline” | 先检索能力；复用组件与模板，不复制Queue/trace等实现 |
| S-A02 | “比较单缓冲和双缓冲” | 调用真实工具，先确认正确性，再比较同口径性能 |
| S-A03 | “queue depth改成0” | 保留用户输入并展示约束错误，不偷偷改成1 |
| S-A04 | “为什么系统变慢” | 查指标/等待，按需开局部trace，提出有针对性的对照 |
| S-A05 | “在pipeline_analytic比较bank数” | 指出该profile不含bank时间；需采用相关profile才能回答 |
| S-A06 | “没有RTL，先比较架构” | 继续可执行探索，明确假设，不强制等待校准 |
| S-A07 | “traffic_only结果数值通过了吗” | 明确当前不能声明数值通过，建议full_data检查 |
| S-A08 | 模拟工具缺失/构建失败/oracle失败 | 分别报告BLOCKED或FAIL；不编造执行、命令或证据 |
| S-A09 | “修改README中的说明” | 只做相关检查，不重新启动整套过程和全量仿真 |
| S-A10 | “补一个通用组件” | 工程产物写回Repo；Skill包仍只保留方法与调用说明 |
| S-A11 | “只验证行为，环境没有SystemC” | 若已选纯Python资产则直接运行，不引入无关编译依赖 |
| S-A12 | “这个模型叫detailed，可以证明cycle-accurate吗” | 检查timing_model/能力/范围；拒绝仅依据名称的推断 |

S-A01/S-A02必须有真实端到端执行；其余可以用小型固定fixture或工具返回样例测试方法，但结果要注明测试方式，不能把mock执行称为真实仿真。

### 7.1 工作类型路由验收

W01–W14的统一验收清单见第14节W-A01–W-A14；八个subskill直接调用与证据复用同时检查，不另设重复编号。

## 8. 扩展 TODO 与验收

| 完成 | ID | 扩展任务 | 启动条件 | 验收 |
|---|---|---|---|---|
| [ ] | X01 | BankedMemory + banked_sram模板 | 要比较SRAM bank/端口 | 配套同bank=16 ticks、异bank=8 ticks微基准；跨bank请求正确拆分 |
| [ ] | X02 | memory_resource profile | 聚合DMA时间不足以解释访存 | 删除被替代的聚合搬运计时；真实事务/竞争，字节守恒；不套用4864/2592 |
| [ ] | X03 | RegBank/IRQ + register_device模板 | 要测试软件控制/寄存器前端 | RW/RO/W1C、byte strobe、mask、同刻set优先、busy重复START与reset |
| [ ] | X04 | DescriptorCodec + fetch前端 | 要分析描述符访问开销/兼容性 | 字节布局固定；非法字段拒绝；记录fetch流量；与命令模式功能一致 |
| [ ] | X05 | PipelinedResource | 要区分latency与启动间隔 | 固定输入下首结果延迟、稳态吞吐和背压均符合配置 |
| [ ] | X06 | 多消费者buffer引用 | 多任务共享输入/输出 | 最后消费者结束前不回收；异常与取消正确减引用 |
| [ ] | X07 | Stream/Credit/AddressRouter | 目标系统确实需要 | credit守恒、背压、包边界、路由错误各有针对性用例 |
| [ ] | X08 | RTL/ISS/详细DDR或NoC后端 | 具体精度或软件需求 | 单独定义对齐范围、参考数据、适配边界和判据后验收 |

X01–X04优先于其他扩展。X08不设置泛化的“误差≤5%”指标；有对应参考和使用范围后再制定，不能为完成计划虚构准确率。

### 8.1 多AXI端口SRAM性能专项：独立验收包

| 完成 | ID | 任务 | 依赖与验收 |
|---|---|---|---|
| [ ] | X09 | multiport_sram_perf模型/配置/负载 | X01 + X07中的Router能力、已有Queue/Observe/Bench；复用而非复制资产 |
| [ ] | X10 | 事务源、拆分重组与响应容量 | 同ID同方向顺序、beat到16 B片段映射、有限outstanding/重组/返回队列；不要求pin-level AXI |
| [ ] | X11 | performance专项比较报告 | 执行示例第14节M0–M8，提供每端口/bank/Router/返回路径指标和证据 |

X09–X11是用户指定的Performance-Oriented ESL验收范围；不因基础包通过而自动通过，也不要求先实现X03寄存器、X04描述符或X08 RTL。X07可只完成本案例用到的Router，不必顺带实现无关Stream/Credit资产。

专项判据权威来源为示例第14.6节，执行清单如下：

| 完成 | ID | 通过条件 |
|---|---|---|
| [ ] | MP01 | M0同bank8 ticks、异bank1 tick；完整128 B beat拆为8片段 |
| [ ] | MP02 | M1–M5无容量/资源上界违规；16×16 B/tick=256 B/tick为bank总服务上限 |
| [ ] | MP03 | M4公平容量预算下比较FIFO与VOQ，展示HOL等待与对照，不把增大buffer冒充策略收益 |
| [ ] | MP04 | M6暂停/恢复返回端时背压传播、无丢失/无限缓冲、最终outstanding为0 |
| [ ] | MP05 | M7受控条件下round-robin grant间隔满足最多7次其他端口grant |
| [ ] | MP06 | M8真实数据、strobe、拆分/重组、同ID顺序与完成可见性正确 |
| [ ] | MP07 | Skill选对performance profile，解释热点/交织/Router瓶颈，列出AXI抽象范围与未校准限制 |

该包完成需要X09–X11、MP01–MP07及U02的真实执行通过。traffic_only仍是可选；若实现，增加同一数据无关短负载与full_data的事务/时间一致性检查，并明确数值未验证。此处的资源上界不是RTL性能误差指标。

## 9. 验收结果怎样记录

不额外写厚报告。工具产出的检查记录和结果文件作为证据，补一页 `acceptance_summary.md` 即可；它属于 Repo 的测试/报告资产体系，不放入 Skill。

摘要至少包含：

| 内容 | 要求 |
|---|---|
| 对象 | Repo/Skill源码revision，必要时记录dirty内容hash，环境版本 |
| 范围 | 基础包 / 多端口SRAM性能专项包 / 其他按需扩展，各自列明 |
| 状态 | 通过、失败、阻塞、未运行项目及原因 |
| 证据 | run/checks/metrics/trace的相对路径或制品链接 |
| 结论 | 基础是否可用、哪些扩展可用、剩余限制 |
| 重现 | 一个环境入口和一组可执行命令 |

只引用既有输出，不把配置、指标抄入多份文档。发现问题时保留失败run，修复后产生新run，不能覆盖失败记录使其看起来从未发生。

## 10. 完成判定与回归范围

**基础开发完成：**R01–R35与S01–S18完成；A01–A16、T01–T12、P01–P07、S-A01–S-A12及C01–C04通过；同时满足第14节的基础真实运行、全部W-A方法覆盖与subskill直接调用要求；按fixture验证的方法与未实现真实后端分别标记。未实现扩展明确列为未覆盖。这里的编号只是任务/测试定位，不是逐级审批流程。

基础必测项的外层测试状态出现FAIL/BLOCKED/NOT_RUN，就说明相应基础工作尚未完成；仍可交付已经可用部分，但不能宣称整体基础验收通过。

| 改动 | 建议回归 |
|---|---|
| README文字 | 文档与引用检查 |
| 单模型功能/参数 | 相关单测 + 一条受影响端到端场景 |
| Queue/Resource/Memory/BMU公共行为 | 对应公共测试 + 基础系统相关正常/异常测试 |
| 指标/trace实现 | A06–A10 + T10 + 基线统计对照 |
| 仲裁/时间策略 | T01–T04 + E0–E6；若故意改合同，先记录预期变更依据 |
| 模板/生成器 | 受影响模板的新名称生成与运行 |
| Skill方法 | 对应S-A场景；只有调用链/编排变化才补端到端 |

不要通过放宽断言或修改解析预期掩盖缺陷。确有架构合同变化时，同时更新规划、测试与限制说明，并保留变更理由。

## 11. 可立即开始的第一批任务

- [ ] 建立Repo骨架、环境检查和三个精简配置格式（R01–R04）。
- [ ] 确定Command/Memory/Event与时间/生命周期的最小合同（R05–R06）。
- [ ] 实现Queue、非流水Resource、ByteStore、BufferPool及少量边界测试（R07–R11）。
- [ ] 同步接入最小counter/时间戳，避免后续每个模型自行埋点（R19–R20的必要部分）。
- [ ] 编写主入口路由与专业subskill合同（S01–S03、S10–S18），基础链先真实跑通。
- [ ] 组合DMA与Compute，先得到T01的正确数据和76 ticks，再扩到整条流水。

这批工作的直接目标是一个可解释、可核对的小闭环；其后沿上文TODO逐项补齐，不先建设大型平台或复杂过程系统。

## 12. 本轮审查修订记录与实施注意事项

| 已修正的问题 | 统一后的约定 | 落点 |
|---|---|---|
| subskill被recipes替代 | 恢复八个独立subskill、十四类工作路由、直接调用与交接 | Skill §3/13–16、S10–S18/U01–U14 |
| 用途与profile混淆 | 四类用途独立于时间/数据/接口；profile为已实现组合 | Repo §2、Skill §4、A13/A14 |
| BankedMemory基础/扩展冲突 | 基础Mini Pipeline不必实现；SRAM专项需要 | Repo组件表、X01/X09 |
| 所有模型被迫SystemC/CMake | 纯行为Python参考允许独立运行 | Repo/Skill、A16 |
| 末次完成可能漏统计 | whole_run含终结事件；分段窗口半开 | Repo观察规则、示例、A15 |
| off档仍要求全部指标 | off保留正确性/终态，性能采集未运行 | Repo、T10相关说明 |
| 负向测试与run状态混淆 | 原run失败保持不变，harness判断预期结果 | A12、T06–T12 |
| Scheduler/资源双重仲裁 | 依赖判定与资源仲裁分工、容量与同tick顺序明确 | Repo接口、示例§6.1 |
| planned能力被当可用 | inspect只把available作为可运行候选 | Repo Schema/CLI、R31 |
| 性能类别缺专门案例 | 多AXI端口SRAM检查交织/HOL/仲裁/背压 | 示例§14、X09–X11/MP01–MP07 |

开工前优先固定Repo中profile字段、接口完成语义、仲裁owner、时间与统计边界。这些选择影响多组件，提前统一收益最大；其余实现组织由开发者自主决定，不新增审批阶段。

所有新增验收项仍为待开发/待运行。此次只检查并修订规划，没有实施ESL Repo/Skill或执行其正式验收。


## 13. 工作类型覆盖的补充TODO

本轮增加方法覆盖，不把14种任务变成14级流程。前述R/S/X编号继续有效；新增R项是基础工具的补充，专用后端仍按专项或扩展交付。所有新项保持未完成。

| 完成 | ID | 资产TODO | 依赖与完成条件 |
|---|---|---|---|
| [ ] | R33 | 组合能力与兼容检查补齐 | 扩展R03/R04/R31；检查端口方向/合同、地址、profile能力、时间owner；错误到具体实例 |
| [ ] | R34 | 工作类型/独立调用测试fixture | common/testing或tests中提供现有/缺失/不兼容/planned候选和工具结果样例；Skill仅存方法 |
| [ ] | R35 | 负载自检与差分支撑 | 在R24基础上检查请求总量、范围、依赖、停止/排空；跨profile对齐数据和终态 |
| [ ] | X12 | 校准支撑资产 | 明确校准需求时启动；参考索引、适配、误差计算、拟合/验证区分与适用域；真实验收需要真实参考 |
| [ ] | X13 | 第三方后端导入与迁移 | 有实际外部模型时启动；来源/许可、依赖、语义映射、适配及消费者测试；基础仅需小fixture验证方法 |

基础新增资产验收：

| ID | 操作与通过条件 | 关联 |
|---|---|---|
| C01 | 合法系统通过；方向/类型/地址冲突及planned候选分别拒绝；错误指出实例和字段 | R33 |
| C02 | 同一物理内存重复owner或旧聚合时间与新内存时间并存，显式配置能被检查发现；不能假称静态证明任意模型内部代码 | R33 |
| C03 | 已有、缺失、不兼容三类fixture导出不同选择/缺口结果；mock结果始终标明 | R34/S18 |
| C04 | 故意错误的字节量、地址范围、依赖/结束条件被负载自检发现；同语义两profile数据/终态对齐 | R35 |

## 14. 工作类型与subskill验收矩阵

本节为验收权威清单；J题详见系统示例第15节。所有W类型都需方法/路由检查；只有声明支持的后端才要求真实执行。基础真实运行与SRAM专项不因方法fixture通过而被替代。

| ID | 工作类型 / 测试题 | 必须证明的方法 | 真实执行范围 |
|---|---|---|---|
| W-A01 | W01 / J01 | 行为任务选最小实现，支持无SystemC的纯Python候选 | 基础：隔离DMA行为单测，相关R14/A16 |
| W-A02 | W02 / J07 | 性能建设交开发；资源/容量/仲裁/背压有模型证据 | SRAM专项：M0–M8及MP01–MP07 |
| W-A03 | W03 / J02 | 已有行为核心增加性能profile，不复制核心/重复计时 | 基础：DMA共享资源微基准和跨profile数据检查 |
| W-A04 | W04 / J03 | 先检索、纯组合，资产齐全时不重新开发 | 基础：T01–T03及已有端到端用例 |
| W-A05 | W05 / J04 | 发现缺口只补必要资产，再返回原组合 | 基础：缺Compute hook的独立fixture实际补齐并跑T01 |
| W-A06 | W06 / J10 | 软件前端用同一核心，明确探索/真实ABI | X03/X04扩展：寄存器/descriptor实际语义检查 |
| W-A07 | W07 / J11 | 限定精化范围，替换时间owner，保持功能合同 | X02或声明的微架构扩展；未实现先验方法 |
| W-A08 | W08 / J08 | 只补负载，独立oracle与自检，不改DUT制造预期 | 基础小负载实际自检；热点/HOL实际运行随SRAM专项 |
| W-A09 | W09 / J05/J06 | 模型能力匹配、控制变量、失败点可见、结论有证据 | 基础E0–E6；bank分析随SRAM专项 |
| W-A10 | W10 / J09 | 保存复现、定位根因、修复后相关回归，不只改timeout | 基础用取消/错误fixture；SRAM返回卡死随专项 |
| W-A11 | W11 / J12 | 匹配参考、分离拟合/验证，缺参考正确报告 | 基础可用标注人工数据的方法fixture；真实校准属X12/X08 |
| W-A12 | W12 / J13 | 来源/兼容/可用性区分，导入后验证再登记 | 基础小候选fixture；真实外部导入属X13 |
| W-A13 | W13 / J14 | 通用实现回Repo，模板不复制公共实现 | 基础Queue积分fixture和消费者相关回归 |
| W-A14 | W14 / J15 | 尊重仅规划/文档停止点，不启动完整模型开发 | 基础文档/引用检查，不要求仿真 |

每条W-A记录method_status、执行方式与证据；若有真实后端，还记录对应运行/验收结果。这是验收摘要的列，不新增模型运行状态体系。路由正确而后端缺失时，只能写“方法通过，执行未覆盖/阻塞”，不能将整个任务标为已实现。

S18还必须覆盖八个subskill各一次独立调用，以及“已有结果复用”和“证据失效”两项：只改README不触发全量回归，改bank映射使相关性能结果失效。独立调用校准缺参考的预期诊断可以通过方法测试，但校准任务本身保持BLOCKED。

### 14.1 完成声明分开写

- **基础可用：**第10节基础任务和检查满足，八个subskill方法覆盖齐全；尚缺的软件前端/详细后端显式列出。
- **SRAM性能专项可用：**X01、所需X07能力、X09–X11及MP01–MP07通过，并有W-A02/W-A08/W-A09对应真实证据。基础通过不自动推出专项通过。
- **扩展可用：**每个宣称的寄存器、descriptor、微架构、校准或外部后端必须单独列实际验收证据。

## 15. 本轮增补记录

恢复“一个主入口 + 八个独立subskill”；建立W01–W14工作目录、输入输出、路由及交接；同步Repo资产支撑、J01–J15任务题、S10–S18与R33–R35/X12–X13、C01–C04和W-A01–W-A14检查。保留原T/M/MP解析预期与既有编号，避免调整方法时改动硬件模型语义。

本轮只更新规划材料，未创建实际Skill、未实现ESL、未执行正式仿真或验收。文档检查通过不能勾选上述研发任务。
