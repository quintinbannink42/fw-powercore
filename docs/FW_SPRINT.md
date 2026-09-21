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

## Next (after FW-INI)

1. E-fuse SM on `protected_gpio` — consume per-channel inrush / OC / retry / latch from `pdmChannelTrip[]` (TS placeholders landed).
2. CAN consume signal list (rusEFI ECU broadcast → pump/fan/output logic). See `pdmCan*` fields.

## Compile / submodule status

See bottom of this file after compile attempt (updated in-pass).

### Initial note

- `ext/rusefi` submodule declared in `.gitmodules` but **not populated** in this stub workspace.
- Per Boss: attempt init + `./compile_firmware.sh` if feasible; if auth/time blocks, document and stop — do not hang.

## Compile result — 2026-09-21 ~06:11 SAST

**PASS** — green firmware compile for PowerCore stub. Identity stayed `powercore` (no fallback to `powercorea`).

- **Command:** `./compile_firmware.sh` (from `/workspace/pdm-rev1/fw-hellen-pdm-razor/`)
- **Exit code:** 0
- **Submodules:** `ext/rusefi` nested submodules init/update recursive completed (network OK)
- **Toolchain note:** non-fatal `/bin/sh: 1: bc: not found` during post-link size calc; link/bin/hex still produced

### Artifacts

| Path | Size (bytes) |
|------|-------------:|
| `ext/rusefi/firmware/build/rusefi.elf` | 28263764 |
| `ext/rusefi/firmware/build/rusefi.bin` | 580960 |
| `ext/rusefi/firmware/build/rusefi.hex` | 1633991 |
| `ext/rusefi/firmware/build/rusefi_crc32.bin` | 580960 |
| `ext/rusefi/firmware/build/rusefi.srec` | 1742942 |

Board-specific generated (outside demos): `generated/controllers/generated/*_powercore.h`, `generated/tunerstudio/generated/rusefi_powercore.ini`, `generated/tunerstudio/generated/signature_powercore.txt`.

Log confirms: `SHORT_BOARD_NAME: powercore`, `FIRMWARE_ID="powercore"`, `PROJECT_CPU=ARCH_STM32F7`, flash0 used ~580960 B / 768 KB.

## FW-INI spike — 2026-09-21

PDM-shaped TunerStudio: replaced Fuel/Ignition/Cranking top-level menus, added HP/ADIO trip + CAN consume UI hooks, dropped LTFT page. See [`TUNERSTUDIO_PDM.md`](TUNERSTUDIO_PDM.md). Identity still `powercore`.
