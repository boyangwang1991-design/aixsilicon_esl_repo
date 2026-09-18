# NPU SRAM architecture exploration

Execution: PASS. Runs: 1045.
Training-selected candidate: **B32_G32_xor0**. Selection frozen before holdout.

Results are resource-model predictions, not measured silicon or RTL calibration.
All NPU runs measure complete closed-loop DAGs, including startup; no steady-state throughput claim.
Traffic-only results have separate SystemC full-data correctness tests. p99 of short runs is descriptive only.

| Candidate | Training makespan/C0 | Worst workload/C0 |
|---|---:|---:|
| B32_G32_xor0 | 0.9683 | 1.0224 |
| B16_G64_xor1 | 0.9713 | 1.0672 |
| B16_G64_xor0 | 0.9713 | 1.0373 |
| B64_G16_xor0 | 0.9729 | 1.0149 |
| B32_G32_xor1 | 0.9732 | 1.0299 |
| B64_G16_xor1 | 0.9778 | 1.0074 |
| B8_G128_xor1 | 0.9857 | 1.1269 |
| B8_G128_xor0 | 0.9885 | 1.1418 |
| REGION | 0.9885 | 1.1333 |
| B64_G16_modulo0 | 0.9998 | 1.0299 |
| B32_G32_modulo0 | 1.0000 | 1.0000 |
| C0 | 1.0000 | 1.0000 |
| B16_G64_modulo0 | 1.0033 | 1.0522 |
| B8_G512_xor3 | 1.0075 | 1.2081 |
| I_B8_xor_flat_fifo | 1.0075 | 1.2081 |
| I_B8_xor_flat_voq | 1.0075 | 1.2081 |
| I_B16_xor_flat_fifo | 1.0174 | 1.2985 |
| B16_G512_xor1 | 1.0176 | 1.2985 |
| I_B16_xor_flat_voq | 1.0176 | 1.2985 |
| I_B8_modulo_flat_fifo | 1.0201 | 1.2081 |
| I_B8_modulo_flat_voq | 1.0201 | 1.2081 |
| B16_G512_xor2 | 1.0222 | 1.2889 |
| C_ECC | 1.0226 | 1.1269 |
| I_B16_modulo_flat_fifo | 1.0245 | 1.3358 |
| I_B16_modulo_flat_voq | 1.0246 | 1.3358 |
| DUAL | 1.0264 | 1.2416 |
| B8_G2048_xor1 | 1.0313 | 1.2667 |
| FIFO_B8_G2048_xor1 | 1.0313 | 1.2667 |
| B8_G128_modulo0 | 1.0341 | 1.1328 |
| O8_B8_G2048_xor1 | 1.0497 | 1.3691 |
| ECC_B8_G2048_xor1 | 1.0504 | 1.3806 |
| B32_G256_xor1 | 1.0676 | 1.6716 |
| B32_G512_xor0 | 1.1012 | 1.6567 |
| H256_B8_G2048_xor1 | 1.1017 | 1.6493 |
| LOCAL_XOR_H | 1.1128 | 1.5333 |
| I_B16_xor_hierarchical_voq | 1.1280 | 1.7481 |
| I_B16_xor_hierarchical_fifo | 1.1281 | 1.7481 |
| I_B8_xor_hierarchical_fifo | 1.1285 | 1.6343 |
| I_B8_xor_hierarchical_voq | 1.1285 | 1.6343 |
| H128_B8_G2048_xor1 | 1.1298 | 1.6940 |
| Gfirst_B8_G2048_xor1 | 1.1310 | 1.6963 |
| I_B8_modulo_hierarchical_fifo | 1.1320 | 1.6119 |
| I_B8_modulo_hierarchical_voq | 1.1320 | 1.6119 |
| I_B16_modulo_hierarchical_voq | 1.1376 | 1.8284 |
| I_B16_modulo_hierarchical_fifo | 1.1383 | 1.8284 |
| R32 | 1.1785 | 1.2800 |
| H64_B8_G2048_xor1 | 1.2518 | 2.1418 |
| ECC_QUARTER | 1.2714 | 1.7152 |

Holdout selected ratio: **0.9968**, worst **1.0102**.
10% improvement goal: False; worst regression <=10%: True.

Scope/limitations: see model docs/verification.md and docs/design.md.
The finite search establishes the best evaluated candidate, not a global optimum.
Resources are explicit bookkeeping proxies; frequency is fixed at nominal 1 GHz.
Random embedding workloads use five paired seeds; their 95% intervals are in ranking.json.
Final archive keeps candidate configurations, aggregate metrics, benchmark metadata and source hashes. Raw per-run traces/build trees were removed after verification at user request. See README.md for the final conclusions and archive boundary.
