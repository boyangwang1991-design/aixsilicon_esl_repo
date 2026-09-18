# ram 设计

真实字节存储，初值为零，可通过 `initial_data` 提供前缀映像。`functional` 不增加服务时间；`resource` 单服务资源时间为 `(setup_ticks + ceil(data_length / bytes_per_tick)) * tick`。两者均消费入站 delay，返回 delay=0。队列与服务中的事务合计受 max_outstanding 限制，满时立即 GENERIC_ERROR；重试策略由调用者决定。

读写在服务完成时提交；byte enable 按 FF/00 循环解释，禁用读字节保持调用者原值。拒绝越界、零长度、空数据、streaming width 小于长度、无效 byte enable；不支持地址回绕。read_only 为真拒绝写。计数器可关闭，字节计数含禁用字节的 payload 长度。

`reset(clear_memory)` 关闭准入并取消所有旧 epoch 事务；clear_memory=true 清零并重载初始映像，false 保留已提交数据。取消返回 GENERIC_ERROR，不留下部分写；等待 idle 后 resume。reset 不清零累计 counters。drain 仅关闭准入，已接受事务正常完成。
