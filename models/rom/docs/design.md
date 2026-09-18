# rom 设计

基于 RAM 的存储与服务实现，以只读策略复用相同生命周期，避免复制时序逻辑。Config 继承 ram::Config；构造时无条件强制 read_only=true。initial_data 是初始化字节前缀，其余地址为零；超过容量的映像拒绝。reset(true) 恢复映像。写事务返回 COMMAND_ERROR，不能通过修改配置解除写保护。

functional/resource、byte enable、容量、计数器和 reset/drain 语义见 [RAM 设计](../../ram/docs/design.md)（源码仓库路径）；安装后的完整 RAM 文档位于同级 ram/docs。尚无映像文件解析器，由顶层加载后传入 vector。
