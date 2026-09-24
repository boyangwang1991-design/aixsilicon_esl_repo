# BM v1 回放合同

输入为 JSON，顶层仅 `schema_version: "npu_mesh_bm/v1"` 和 `rows`。
每行必填 id/op/address/bytes；id 非负且唯一。可选 release（cycle，默认0）、source/router
索引、context、axi_id、depends（先前行 id 列表）、data/enables（十六进制逐字节）、
destinations（COPY 一个，MULTICAST 多个地址）、category（字母数字下划线）。
READ/WRITE 1..max_transfer bytes，FENCE bytes=0；DMA 可由子事务拆分。
address/length 按字节，无虚拟地址翻译，禁止溢出、非法 region、未知字段和依赖环。

```json
{"schema_version":"npu_mesh_bm/v1","rows":[
 {"id":0,"op":"WRITE","address":16,"bytes":4,"data":"01020304","source":0},
 {"id":1,"op":"COPY","address":16,"bytes":4,"destinations":[65552],"depends":[0]},
 {"id":2,"op":"READ","address":65552,"bytes":4,"source":1,"depends":[1]}
]}
```

后继只在所有依赖 TASK_DONE 成功后准入。运行异常返回非零；不把失败点放入性能比较。
release_to_done 包括准入等待和依赖等待，accepted_to_done 是已接受事务时延；两者不混用。
量测整个有限批次，包含 fill/drain，无 warmup 剔除，p99 用 nearest-rank；小样本仅是批次
经验分位数。合成 mixed/prefill/decode/hotspot/kv_migration/multicast 场景不执行 GEMM/Attention。
每次保存 workload.json，后续用户真实 trace 可转换为同一接口；转换必须保留依赖、源/目标
映射和实际 payload，不凭 host 时间推导硬件周期。

独跑 decode 使用零初始化内存上的 READ；mixed 中 READ 依赖对应 WRITE。
这两个场景的依赖条件不同，不能直接据此计算严格的 Decode 干扰倍率。
