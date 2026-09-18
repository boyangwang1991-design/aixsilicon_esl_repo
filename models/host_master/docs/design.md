# host_master 设计

事务激励器，不是 CPU/ISS。公开 read/write 同步函数供顶层 SC_THREAD 调用，32-bit socket 承载 byte vector。write 复制调用者数据；read 更新调用者 buffer。payload 不逃逸调用栈；转发响应，消费目标遗留 delay 一次。不引入额外服务时延。

max_outstanding 限制同时调用数；满、drain、空数据或非法进程调用返回 GENERIC_ERROR。drain 后等待 idle，再 resume。没有脚本解析器、IRQ 接口、reset/cancel；系统 reset 必须先排空 host 和 bus，再 reset 存储。
