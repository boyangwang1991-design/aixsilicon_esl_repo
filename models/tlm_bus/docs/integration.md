# tlm_bus 集成

公开头文件 `<tlm_bus/model.hpp>` 与 `<tlm_bus/config.hpp>`；命名空间 `aix::esl::tlm_bus`。构造 `Model(sc_module_name, Config)`；每实例独立状态，顶层拥有模型和 Config。必需端口见 [manifest](../model.yaml)，漏绑定由 SystemC elaboration 报错。

```cmake
# 源码树：ESL_REPO 指向资产仓库
add_subdirectory("${ESL_REPO}/models/tlm_bus" "models/tlm_bus")
target_link_libraries(my_system PRIVATE aix::esl::tlm_bus)
# 安装包：用以下两行替换 add_subdirectory/链接行
find_package(AixEslTlmBus 0.1 CONFIG REQUIRED)
target_link_libraries(my_system PRIVATE aix::esl::tlm_bus)
```

源码与安装二选一；ROM 自动引入 RAM 依赖，重复接入前先检查 target 是否已存在。SystemC 依赖从 SYSTEMC_HOME/CMAKE_PREFIX_PATH 解析，采用仓库 environment 合同指定版本；模型不创建仿真入口、不修改全局时间精度、不下载依赖。安装不携带 SystemC runtime，消费者必须使用相同兼容 ABI。

socket 数据宽度 32，不限制 generic payload 地址为 32 位。地址是字节地址，数据按字节透传，不隐含整数大小端转换。RAM/ROM 接收局部地址；host/bus 使用全局地址。仅 LT b_transport，调用者应在 SC_THREAD/SC_CTHREAD 中阻塞调用并保持 buffer 有效至返回。没有 AT、DMI、debug 通路（目标返回 false/0）；不支持 cache coherence。

`idle()`、`idle_event()`、`request_drain()`、`resume()` 提供排空协议：先关闭所有上游准入，再依次等待未 idle 的组件事件。满/关闭准入返回 GENERIC_ERROR，未接受请求不会隐式重试。内存 reset 能取消事务；host/bus 仅支持 drain，不能宣称通用热复位。没有时钟/reset 信号端口。

可执行外部消费者与完整构建命令见 [basic_system](../../../examples/basic_system/README.md)。
