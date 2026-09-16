# PowerCore firmware sprint notes

**When:** 2026-09-16 ~08:21 SAST  
**Owner:** Code Jeoff · **Reports to:** Boss  
**Product:** PowerCore (standalone PDM — not ECU, not HELLCORE)

## Pin freeze ack

Code Jeoff acknowledges pin freeze against Jeoff hardware docs:

- `/workspace/hellen-pdm-razor/PINMAP.md`
- `/workspace/hellen-pdm-razor/HARDWARE_BOM.md`
- `/workspace/hellen-pdm-razor/CONNECTOR.md`

No invented pins. KiCad / copper (`pdmrazora`) untouched. Not waiting on Jeoff fab for this stub pass.

## What changed in this stub pass (FW-0a / FW-0b)

| Area | Change |
|------|--------|
| Sense constants | `HP_AMPS_PER_VOLT = 19.3f`, `ADIO_AMPS_PER_VOLT = 4.26f` (BOM starters); VBATT divider remains **11.0f** |
| protected_gpio | Unchanged mapping: **HP1–4 + ADIO1–4** per PINMAP |
| Identity | `SHORT_BOARD_NAME=powercore`, `FIRMWARE_ID=powercore` (fallback only if rejected: `powercorea`) |
| Docs | README PowerCore-branded + PINMAP-aligned channel table; new `IDENTITY.md` |
| Connectors | Draft YAML under `connectors/` (SuperSeal + logical channels / trip placeholders) for PDM INI |
| This file | Sprint log |

## Explicit constraints

- **AmpsPerVolt calibration is blocked on first silicon.** BOM starters land now; two-point bench cal later.
- **No parallel protection layer.** E-fuse SM builds on `protected_gpio` / `tdg-pdm8` patterns only.
- Do not touch `/workspace/hellen-pdm-razor` KiCad or HELLCORE.

## Next: FW-INI spike (do not expand here)

1. INI strip research (`tdg-pdm8`, `MINIMAL_PINS`, board overrides) → PDM-only TunerStudio pages.
2. E-fuse SM on `protected_gpio` — expose per-channel inrush limit, inrush window, OC limit, trip time, retry/latch (fields already stubbed in `connectors/pdm_channels.yaml`).
3. CAN consume signal list (rusEFI ECU broadcast → pump/fan/output logic).

## Compile / submodule status

See bottom of this file after compile attempt (updated in-pass).

### Initial note

- `ext/rusefi` submodule declared in `.gitmodules` but **not populated** in this stub workspace.
- Per Boss: attempt init + `./compile_firmware.sh` if feasible; if auth/time blocks, document and stop — do not hang.
