# PowerCore connectors — drafts for PDM INI codegen

**Status:** DRAFT (FW-0b). Not yet wired into rusEFI config generation.

These YAML files name PowerCore channels for a **PDM TunerStudio** environment (outputs, current sense, e-fuse trip params, CAN / system). They are **not** engine-ECU injector/ignition/crank fields.

## Files

| File | Role |
|------|------|
| [`superseal26.yaml`](superseal26.yaml) | Physical AMP SuperSeal 1.0 26-way (Link “Connector C”) — rusEFI-style `pins` / `info` |
| [`pdm_channels.yaml`](pdm_channels.yaml) | Logical PDM channel map: HP1–4, ADIO1–8 (EN / I / V / pull-up), system nets, **trip param placeholders** for INI |

## Schema notes

- Physical connector YAML follows [rusEFI Connector Mapping](https://wiki.rusefi.com/Connector-Mapping/) (`pin`, `id`, `ts_name`, `class`, `function`, `type`, `info`).
- `pdm_channels.yaml` extends that with a documented `pdm_meta` / `channels` / `trip_params` block for future INI codegen. Upstream codegen does **not** consume this yet — treat as the P0 contract for FW-INI.
- Authoritative pins: `/workspace/hellen-pdm-razor/PINMAP.md`, `CONNECTOR.md`, `HARDWARE_BOM.md`.
- Product title in `info`: **PowerCore / PDM**.

## Next (FW-INI spike)

1. Research how `tdg-pdm8` / custom boards strip ECU INI pages (`MINIMAL_PINS`, board overrides).
2. Map `trip_params` into TunerStudio fields on top of `protected_gpio`.
3. Promote drafts → live `connectors/*.yaml` once rusEFI submodule is populated and codegen path is confirmed.
