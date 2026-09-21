# PowerCore

Custom [rusEFI](https://github.com/rusefi/rusefi) firmware for **PowerCore** — a standalone automotive **power distribution module** (PDM) on Hellen **mega-mcu144** (STM32F767), aimed at **Link Razor PDM** I/O parity and CAN compatibility with a main rusEFI ECU.

PowerCore is **not** an engine ECU. TunerStudio must present a **PDM** environment (outputs, current, trips, CAN consume / output logic), not a full automotive ECU page set.

**Former stub / working name:** `hellen-pdm-razor`  
**Identity:** `SHORT_BOARD_NAME` / `FIRMWARE_ID` = `powercore` (see [IDENTITY.md](IDENTITY.md))  
**Firmware owner:** Code Jeoff · **Reports to:** Boss

## Sibling docs (authoritative hardware)

Hardware (Hellen-One carrier, PROFET stages, SuperSeal) is owned by Jeoff — **do not invent pins**:

| Doc | Location |
|-----|----------|
| Product brief | [`../POWERCORE_BRIEF.md`](../POWERCORE_BRIEF.md) |
| Firmware SoW | [`../SCOPE_OF_WORKS.md`](../SCOPE_OF_WORKS.md) |
| Pin map | [`../../hellen-pdm-razor/PINMAP.md`](../../hellen-pdm-razor/PINMAP.md) |
| BOM / sense | [`../../hellen-pdm-razor/HARDWARE_BOM.md`](../../hellen-pdm-razor/HARDWARE_BOM.md) |
| Connector | [`../../hellen-pdm-razor/CONNECTOR.md`](../../hellen-pdm-razor/CONNECTOR.md) |
| Also in this tree | [`../PINMAP.md`](../PINMAP.md), [`../CONNECTOR_RAZOR.md`](../CONNECTOR_RAZOR.md), [`../REV1_SPEC.md`](../REV1_SPEC.md), [`../BLOCK_DIAGRAM.md`](../BLOCK_DIAGRAM.md) |

KiCad project stays **`pdmrazora`** (hellen-one-safe).

## Channels (rev 1) — PINMAP + `board_configuration.cpp`

| Group | Count | Enable | Current sense (I) | Voltage sense (V) | Protection |
|-------|------:|--------|-------------------|-------------------|------------|
| High-power HP1–4 | 4 | `H144_OUT_PWM1`–`4` | `H144_IN_AUX1`–`4` ANALOG | — | e-fuse SM @ 80 A inrush / 60 A OC; AmpsPerVolt **19.3** starter |
| ADIO1–4 | 4 | `H144_OUT_PWM5`–`8` | MAP1 / MAP2 / MAP3 / O2S | TPS / PPS / TPS2 mux / PPS2 mux | e-fuse SM @ 20 A inrush / 10 A OC; AmpsPerVolt **4.26** starter |
| ADIO5–8 | 4 | `H144_OUT_IO5`–`8` | O2S2 / RES1 / RES2 / RES3 | CLT / IAT / AT1 mux / AT2 mux | e-fuse bank `PROTECTED_PIN_8-11`; ADIO6–8 SW OC waits on ADC3 |
| ADIO pull-ups | 8 | `H144_OUT_IO9`–`13`, `H144_GP_IO1`–`3` | — | — | Soft 4k7 to SENSOR_5V |

Also: CAN on `H144_CAN_*`, VBATT on `H144_IN_VBATT` (divider **11.0**), `H144_GP8` **PWR_EN** for SENSOR_5V / switched analogs.

Upstream pattern: `tdg-pdm8` + rusEFI `protected_gpio` with the e-fuse SM filled in — **no parallel protection layer**. See [`docs/EFUSE.md`](docs/EFUSE.md).

## Connectors / TunerStudio

Draft rusEFI-style YAML under [`connectors/`](connectors/) (pin names for INI
codegen). TunerStudio is **PDM-shaped**: see [`docs/TUNERSTUDIO_PDM.md`](docs/TUNERSTUDIO_PDM.md)
for how to regenerate `generated/tunerstudio/generated/rusefi_powercore.ini`.
E-fuse SM: [`docs/EFUSE.md`](docs/EFUSE.md). CAN consume + status DBC:
[`docs/CAN.md`](docs/CAN.md).

## Bootstrap

```bash
git clone <this-repo>
cd fw-hellen-pdm-razor   # or fw-powercore when renamed
git submodule update --init --recursive
./compile_firmware.sh
```

Prefer starting from [rusefi/fw-custom-hellen144-f4](https://github.com/rusefi/fw-custom-hellen144-f4) (already Hellen 144) with `PROJECT_CPU=ARCH_STM32F7`, or fork [rusefi/fw-custom-example](https://github.com/rusefi/fw-custom-example), replace `meta-info.env` / `board.mk` / `board_configuration.cpp` with these files, set `PROJECT_CPU=ARCH_STM32F7`, and keep the rusefi submodule.

GitHub Actions: `.github/workflows/build-firmware.yaml` (needs submodule + optional `MY_REPO_PAT`).

## Bring-up checklist

1. Flash, confirm USB + CAN alive, LEDs on module.
2. Toggle `auxOutputPins[0..3]` (HP) with no load; confirm gate drive.
3. Apply known load; **calibrate** `HP_AMPS_PER_VOLT` / `ADIO_AMPS_PER_VOLT` (BOM starters only until silicon).
4. Verify trip: inrush window, delayed OC, fast short, retry/latch ([`docs/EFUSE.md`](docs/EFUSE.md)).
5. Enable ADIO pull-ups; check open-circuit voltage ~5 V through 4k7.
6. Point at a rusEFI ECU and enable consume/status ([`docs/CAN.md`](docs/CAN.md)).

## Open firmware work

- ADC3 analog for ADIO6–8 (`H144_IN_RES1-3`) so software OC matches ADIO1–5
- Over-temp staged shutdown (no TS field yet)
- Optional HP DIR pins (`H144_OUT_IO1`–`4`) for half/full bridge

Sprint notes: [`docs/FW_SPRINT.md`](docs/FW_SPRINT.md)

## License

Firmware contributions follow rusEFI licensing for files derived from that project.
