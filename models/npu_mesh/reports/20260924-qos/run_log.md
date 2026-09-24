# 工作记录

按用户continue指令实现NIU背景整形和SRAM入口服务区分。新增逻辑字节token bucket、
Packet context传播、优先/老化入口选择、原生在途上限及trace收费字节；默认关闭。
微基准发现同cycle到达请求经优先排序后丢失原始FIFO顺序，已用入口sequence修复并复验。
首次全回归暴露manifest缺少新增参数，已补齐参数/capability并运行最终回归。
最终49CTest、51Mesh Python、94共享Python、7BM及源码/搬迁安装消费者通过，make check/precommit通过。
12点完整相同任务集扫描和额外age512单变量诊断通过，源码/输入指纹核对一致。
限制与负收益点保留在report.md，未将QoS实现宣称为端到端保证。未提交、未发布。
