"""R03/R04：精简 model/system/experiment 配置格式与 Factory。

合同（规划 §7.2/§9）：
- model.yaml：schema_version / id(VLNV aixsilicon:*) / factory / model_kinds /
  profiles / ports / parameters / assumptions。
- system.yaml：实例与连接（instance: 模型 id + 参数；connect: 端口对）。
- experiment.yaml：场景与参数扫描（sweep 点、基线、指标）。
- 未知参数/非法连接报错；静态 DAG 环拒绝；输出最终生效值，不静默丢失。
- 选择不支持的 profile/前端/数据模式必须拒绝。
"""
from __future__ import annotations

import re

from .errors import ConfigError

VLNV_RE = re.compile(r"^aixsilicon:[a-z0-9_]+:[a-z0-9_]+:[0-9]+\.[0-9]+\.[0-9]+$")


def validate_vlnv(vlnv: str, field: str = "id") -> None:
    """VLNV 一律 aixsilicon:*（canonical guard 同规则）。"""
    if not VLNV_RE.match(vlnv):
        raise ConfigError(f"{field}: 非法 VLNV {vlnv!r}（须 aixsilicon:*）", field=field, value=vlnv)


def validate_model(model: dict, available_profiles: set | None = None) -> dict:
    """校验 model.yaml：必填字段、VLNV、profile 引用。返回最终生效配置。"""
    for key in ("schema_version", "id", "factory", "model_kinds", "profiles"):
        if key not in model:
            raise ConfigError(f"model.yaml 缺字段 {key!r}", field=key)
    validate_vlnv(model["id"])
    if not isinstance(model["profiles"], dict) or not model["profiles"]:
        raise ConfigError("model.yaml profiles 必须是非空映射", field="profiles")
    if available_profiles is not None:
        unknown = set(model["profiles"]) - available_profiles
        if unknown:
            raise ConfigError(f"引用未实现的 profile: {sorted(unknown)}", field="profiles")
    return dict(model)


def validate_system(system: dict, available_models: set | None = None) -> dict:
    """校验 system.yaml：instances 与 connections。"""
    if "instances" not in system or not isinstance(system["instances"], dict) \
            or not system["instances"]:
        raise ConfigError("system.yaml 缺 instances（非空映射）", field="instances")
    if "connections" not in system or not isinstance(system["connections"], list):
        raise ConfigError("system.yaml 缺 connections（列表）", field="connections")
    for inst_id, spec in system["instances"].items():
        model_id = spec.get("model") if isinstance(spec, dict) else spec
        if available_models is not None and model_id not in available_models:
            raise ConfigError(f"实例 {inst_id} 引用不可用模型 {model_id!r}", field=f"instances.{inst_id}.model")
    return dict(system)


def validate_experiment(experiment: dict) -> dict:
    """校验 experiment.yaml：sweep 点、基线、指标。"""
    if "system" not in experiment:
        raise ConfigError("experiment.yaml 缺 system 引用", field="system")
    if "sweep" not in experiment or not isinstance(experiment["sweep"], dict):
        raise ConfigError("experiment.yaml 缺 sweep（非空映射）", field="sweep")
    return dict(experiment)


class ModelFactory:
    """R04：模型 Factory——按 factory 名实例化模型。

    实际模型类通过 register() 注册；未知 factory 拒绝（不静默降级）。
    """

    def __init__(self):
        self._registry = {}

    def register(self, factory: str, cls) -> None:
        self._registry[factory] = cls

    def create(self, factory: str, **params):
        if factory not in self._registry:
            raise ConfigError(f"未知 factory {factory!r}（未注册或不可用）", field="factory", value=factory)
        return self._registry[factory](**params)
