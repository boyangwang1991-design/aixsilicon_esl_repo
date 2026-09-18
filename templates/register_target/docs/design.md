# Behavior

Address 0 is a four-byte little-endian read/write register. Initial value and per-call annotated latency come from Config. No byte enables, DMI, debug or AT. Errors have no side effects or time cost.

This template executes synchronously at call time and adds latency to the supplied delay; it does not wait or retain payloads. Caller synchronization owns the annotation. It is not a completion-time register model. idle is always true; drain closes admission, reset restores the initial value and closes admission; resume reopens it. No clock/reset signal or idle event is required for this no-outstanding model.
