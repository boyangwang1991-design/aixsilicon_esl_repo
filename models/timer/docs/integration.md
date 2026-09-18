# timer 集成

公开 `<timer/model.hpp>` 与 `<timer/config.hpp>`，构造 `aix::esl::timer::Model(sc_module_name, Config)`；offset 常量在 reg 命名空间。

registers 是必需 LT multi target socket（32-bit、至少一个绑定）；reset 是必需高有效 sc_in<bool>；irq 是必需高有效 sc_out<bool>。

事务、时间、字节序、reset/drain、错误和 delta-cycle 规则唯一见 [MMIO32 合同](../../../contracts/mmio32.md)。公开 idle/idle_event/request_drain/resume 用于上层生命周期管理。

```cmake
# 源码接入
add_subdirectory("${ESL_REPO}/models/timer" "models/timer")
target_link_libraries(my_system PRIVATE aix::esl::timer)
# 或安装接入：以 find_package 替换 add_subdirectory
find_package(AixEslTimer 0.1 CONFIG REQUIRED)
```

SYSTEMC_HOME/CMAKE_PREFIX_PATH 指向兼容 C++17 的 SystemC 3.0.2；模型不拥有 sc_main 或全局时间精度。完整消费者见 [interrupt_system](../../../examples/interrupt_system/README.md)。
