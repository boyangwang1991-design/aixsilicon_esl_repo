"""mini_pipeline 示例系统（R18）。

系统：外存输入 → DMA 搬入 → 向量计算 → DMA 回写。
复用 DMA/Compute/BMU/Scheduler 与公共组件，本示例只做组合配置。
"""
