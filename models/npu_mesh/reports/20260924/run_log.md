# 执行记录

- 只读发现总线契约、文王工程 ESL 规划、Repo 资产与构建合同；用户原有 untracked 契约保留。
- 使用 esl-development-suite 方法，未启用子代理；资产仅写 ESL 仓库。
- 环境：`esl doctor` 确认 SystemC 3.0.2、g++8.5、CMake4.4.3、Python3.12.13。
- 初始直接 git status 为读 AGENT.md 前的只读探测；此后仓库状态使用 aix repo status esl。
- 沙箱中 uv 子进程完成后父进程挂起，按工作区已知约定使用 require_escalated 执行原 uv 命令。
  一次自动审核超时，按工具提示重试后执行成功，无用户批准遗留事项。
- 初轮 mask 用例失败：公共 ByteStore 要求0/255 enable，修复入口归一化并检查写返回值。
- 增补 head 已预留目标槽但 tail 前超时的用例，修复迟到 tail 释放资源。
- 最终：`esl npu-mesh validate --output runs/mesh-validation-20260924-final`，PASS。
- 最终：`esl npu-mesh explore --output runs/mesh-sweep-20260924-final`，20点 PASS。
- `pytest tests/test_cli_contracts.py tests/test_repository_layout.py tests/test_mesh.py -q`，65项 PASS。
- workflow根 `make check PYTHON='uv run --no-sync python'`、`pre-commit run --all-files`，PASS。
- 未 commit/push；未修改文王正式规格或把模型局部 PASS 写成产品门禁通过。
