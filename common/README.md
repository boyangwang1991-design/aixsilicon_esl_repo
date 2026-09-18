# SystemC 公共实现

目标模型内部共用组件位于 systemc/include/aix/esl：ByteStore、BlockingGate/BlockingLease。Mmio32 是经 timer/IRQ 系统回归的私有 helper，访问规则见 [合同](../contracts/mmio32.md)。模型通过私有 include 复用，不要求消费者访问这些实现头文件。

公开模型 API 不暴露内部组件类型。公共接口如需对外发布，应另有版本化 target、安装与消费验证，不能把当前内部 header 视作已发布 ABI。

历史 Python 公共组件唯一位于 [reference/legacy_python/common](../reference/legacy_python/common)，与 SystemC 目标状态分开。构建和回归见 [基础系统](../examples/basic_system/README.md)。
