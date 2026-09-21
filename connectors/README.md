# PowerCore connectors — drafts for PDM INI codegen

**Status:** DRAFT (FW-0b). PinoutLogic-facing YAML lives here; extended trip-param contract is under `drafts/`.

These YAML files name PowerCore channels for a **PDM TunerStudio** environment (outputs, current sense, e-fuse trip params, CAN / system). They are **not** engine-ECU injector/ignition/crank fields.

## Files

| File | Role |
|------|------|
| [`powercore_pins.yaml`](powercore_pins.yaml) | Logical TS pin names (HP/ADIO/PROTECTED_PIN/current) — PinoutLogic input |
| [`superseal26.yaml`](superseal26.yaml) | Physical AMP SuperSeal 1.0 26-way (Link “Connector C”) |
| [`drafts/pdm_channels.yaml`](drafts/pdm_channels.yaml) | Extended channel map + **trip param placeholders** for INI (not PinoutLogic) |

## Schema notes

- Physical / pin YAML follows [rusEFI Connector Mapping](https://wiki.rusefi.com/Connector-Mapping/) (`pin`, `id`, `ts_name`, `class`, `function`, `type`, `info`).
- `drafts/pdm_channels.yaml` adds `pdm_meta` / `channels` / `trip_params` for future INI codegen. Kept out of this directory root so PinoutLogic does not parse non-standard keys.
- Authoritative pins: `/workspace/hellen-pdm-razor/PINMAP.md`, `CONNECTOR.md`, `HARDWARE_BOM.md`.
- Product title in `info`: **PowerCore / PDM**.

## Next (FW-INI spike)

1. Research how `tdg-pdm8` / custom boards strip ECU INI pages (`MINIMAL_PINS`, board overrides).
2. Map `trip_params` into TunerStudio fields on top of `protected_gpio`.
3. Promote drafts → live INI once codegen path is confirmed.
