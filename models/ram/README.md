# ram

SystemC 3.0.2 / C++17 可复用 LT 模型。资产状态唯一见 [registry](../../registry.yaml)，能力见 [model.yaml](model.yaml)。

- [设计与限制](docs/design.md)
- [集成合同](docs/integration.md)
- [验证与复现](docs/verification.md)

目录：`systemc/include/ram/` 公开 Config/Model；`systemc/src/` 实现；`docs/` 文档；`model.yaml` 能力描述；`CMakeLists.txt` 源码接入与安装导出。无需 Python 算法参考模型；这里不建立空 Python 目录。RAM 专项测试在 RAM 的 tests/systemc；组合测试统一在 [basic_system](../../examples/basic_system/README.md)，避免复制。

此版本为基础抽象，不代表周期精确 RTL 或硬件性能校准。
