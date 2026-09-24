# PowerCore electronic fuse (e-fuse)

PowerCore trip settings in TunerStudio (`pdmChannelTrip[12]`) are consumed by
firmware. This is the rusEFI `protected_gpio` pattern (OutputPin + ISENSE ADC +
`gpiochip_register` at `PROTECTED_PIN_*`) with the state machine rusEFI left as
`// TODO: smarter state machine`.

Do **not** add a second GPIO protection layer. The board chip **replaces**
`protectedGpio_add()` so it can intercept `writePad` (required for latch).

Identity stays **`powercore`**.

## Channels

| Index | Name | Enable | Current sense | Software OC |
|------:|------|--------|---------------|-------------|
| 0-3 | HP1-4 | `H144_OUT_PWM1-4` | `H144_IN_AUX1-4` | yes |
| 4-7 | ADIO1-4 | `H144_OUT_PWM5-8` | MAP1/2/3 / O2S | yes |
| 8 | ADIO5 | `H144_OUT_IO5` | `H144_IN_O2S2` | yes |
| 9-11 | ADIO6-8 | `H144_OUT_IO6-8` | RES1/2/3 (not `EFI_ADC` yet) | pin owned; OC waits on ADC3 |

Assign what each output does under **PowerCore → Outputs** (Fuel Pump, Fan 1,
Fan 1 PWM, Starter Control, GP PWM, Main relay, Narrowband O2 heater, A/C
Relay, and the other ECU output names). HP1–4 and ADIO1–8 are the pin list
on those dialogs. ADIO5–8 are on/off on Razor (GP PWM frequency 0, or an
on/off ECU function). ADIO6–8 software OC still waits on ADC3.

## Behaviour (Link Razor-style, mapped onto existing TS fields)

On commanded **ON**:

1. **Inrush window** (`inrushWindowMs` after the rising edge). Current between
   `ocLimitA` and `inrushLimitA` is allowed. `ocLimitA` is not applied yet.
2. **Fast short** (any stage): `amps > inrushLimitA` turns the output off
   immediately.
3. **Overcurrent** (after the window): `amps > ocLimitA` for `tripTimeMs`
   turns the output off.
4. Sense failure (valid `EFI_ADC` channel, conversion invalid) trips the same
   as a short.

Recovery:

- `latchOnFault = yes` **or** `retryCount = 0`: latched off.
- else: wait **1000 ms**, retry, up to `retryCount` times, then latch.
- Unlatch / reset retries: command **OFF for ≥ 50 ms**, a trip-field burn, or
  power cycle. PWM off-times shorter than 50 ms do not reset the inrush window.

`getDiag()` on `PROTECTED_PIN_*` is `PIN_OVERLOAD` while tripped or latched.

Defaults (fresh tune): HP inrush 80 A / OC 60 A / window 50 ms / trip 20 ms /
3 retries; ADIO inrush 20 A / OC 10 A / window 20 ms / trip 10 ms / 3 retries.

Host tests (no toolchain): `firmware/run_pdm_efuse_sm_test.sh`.

## Live data

- HP1-4 current: Aux Linear 1-4 (unchanged).
- ADIO1-8 current: firmware writes `outputChannels.luaGauges[0-7]`. ADIO6-8
  read **0 A** until RES pins are `EFI_ADC`.

CAN status TX publishes the same currents plus commanded/actual/fault bits
(see [`CAN.md`](CAN.md)).

## CAN consume

Landed. See [`CAN.md`](CAN.md). `pdmCanConsumeEnable` / `pdmCanConsumeBaseId`
ingest rusEFI ECU verbose frames; `pdmCanStatusEnable` / `pdmCanStatusBaseId`
publish DBC frames. Consume drives the Fuel Pump / Fan / Fan 2 / O2 heater /
Main relay pins from Outputs (legacy HP1–3 / ADIO1 / ADIO5 if those pins are
None). The e-fuse SM still owns the protected bank.

## What is left

1. Optional: ADC3 analog for ADIO6-8 RES1-3 so software OC matches ADIO1-5.
2. Over-temp staged shutdown (Razor has a per-pin temperature limit; no TS
   field yet).
