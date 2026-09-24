# PowerCore CAN — rusEFI ECU consume + PDM status

PowerCore is a **standalone PDM**. It does not run the engine. A main rusEFI
ECU broadcasts live data; PowerCore consumes that bus, switches e-fuse
protected outputs (fuel pump / fans / …), and publishes channel current and
faults.

DBC stub: [`firmware/powercore_pdm.dbc`](../firmware/powercore_pdm.dbc)
(SavvyCAN / CANalyzer). Full ECU verbose layout:
[rusEFI_CAN_verbose.dbc](https://github.com/rusefi/rusefi/blob/master/firmware/controllers/can/rusEFI_CAN_verbose.dbc).

Identity stays **`powercore`**.

## Point this PDM at a rusEFI ECU

1. **Wire CAN** — PowerCore `H144_CAN_TX` / `H144_CAN_RX` to the ECU CAN bus
   (twisted pair, 120 Ω termination at the ends). SuperSeal CAN pins: see
   connector docs.
2. **Match baud** — default **500 kbit/s** on both nodes
   (`canBaudRate` = 500kbps). TunerStudio: CAN-bus → CAN Bus Settings.
3. **On the ECU** (not this firmware):
   - Enable **CAN write**.
   - Enable **rusEFI verbose / CAN broadcast** (`enableVerboseCanTx`).
   - Set **rusEFI CAN data base address** (`verboseCanBaseAddress`) to a free
     11-bit range. Factory default is **`0x200`**.
   - Do **not** also enable verbose TX on the PDM at the same base — the PDM
     is a consumer.
4. **On the PDM** (PowerCore → CAN consume / status):
   - Enable **Consume ECU broadcast**.
   - Set **Consume base ID** to the ECU `verboseCanBaseAddress`
     (default `0x200` / 512).
   - Enable **Publish PDM status**.
   - Set **Status base ID** to a free range that does **not** overlap the
     ECU verbose block (default `0x240` / 576). ECU verbose uses
     base+0 … base+11, so `0x200`–`0x20B` is reserved when the ECU uses
     `0x200`.
5. Burn and power-cycle if TunerStudio marks CAN pins as requiring it.

Assign the outputs under **PowerCore → Outputs** (Fuel Pump, Fan 1, Fan 2,
Narrowband O2 heater, Main relay). Consume drives those ECU pins:

| ECU signal (BASE0) | TunerStudio field | Fresh tune | If the field is None |
|--------------------|-------------------|------------|----------------------|
| `FuelPumpAct` | Fuel Pump output | HP1 | legacy HP1 |
| `Fan` | Fan Pin | HP2 | legacy HP2 |
| `Fan2` | Fan 2 | HP3 | legacy HP3 |
| `EGOHeatAct` | Narrowband O2 heater output | ADIO1 | legacy ADIO1 |
| `MainRelayAct` | Main Relay Pin | None | legacy ADIO5 |

Lua PWM and GP PWM on a pin that an ECU function uses are forced **None**
so they cannot fight `writePad`. A legacy pin is also released while consume
is on and that function's pin is still None. If you assign Fuel Pump to a
channel another legacy role wanted (for example HP2), that legacy role is
dropped — assign Fan explicitly.

Fan **PWM mode** (Fan 1 PWM / Fan 2 PWM) keeps the local PWM curve. Consume
does not override a fan while PWM mode is on; use on/off Fan for CAN.

With CAN consume **off**, the assigned ECU outputs follow local rusEFI logic
(fuel-pump prime, fan vs CLT, and so on). Fresh tunes set prime duration to
0 and "Disable when engine stopped" on both fans so a missing CLT sensor does
not turn a fan on. **Main relay** is left None on a fresh tune: this firmware
builds with main-relay control off, so an assigned Main relay pin is held
**ON** until consume is enabled and then follows `MainRelayAct`.

If no matching ECU frame arrives for **500 ms**, the consumed outputs are
commanded **off** (timeout failsafe). Re-appear of BASE0/1/3 clears it.

Outputs still go through the e-fuse SM (`docs/EFUSE.md`): inrush / OC / short
/ retry / latch apply to CAN-driven channels the same as Lua-driven ones.

## Frames consumed (`pdmCanConsumeBaseId`, default 0x200)

| ID | rusEFI name | What PowerCore uses |
|----|-------------|---------------------|
| base+0 | Status / BASE0 | Pump, fan, fan2, O2 heater, main relay bits |
| base+1 | Speeds / BASE1 | RPM (echoed on status TX) |
| base+3 | Sensors1 / BASE3 | CLT (decoded; reserved for later logic) |

Bit packing matches `can_verbose.cpp` / `rusEFI_CAN_verbose.dbc` (Intel,
little-endian). Example: `FuelPumpAct` is bit 34 of BASE0 (byte 4, bit 2).

## Frames published (`pdmCanStatusBaseId`, default 0x240)

Period: 50 ms while **Publish PDM status** is on (`canWriteEnabled` must stay
enabled).

| ID | Name | Payload |
|----|------|---------|
| base+0 | `PDM_STATUS0` | Commanded / actual / fault bits for HP1–4 + ADIO1–8; ECU-alive / consume / timeout flags; rolling counter |
| base+1 | `PDM_CURR_HP` | HP1–4 current, uint16 LE, **0.01 A/bit** |
| base+2 | `PDM_CURR_ADIO14` | ADIO1–4 current, same scale |
| base+3 | `PDM_CURR_ADIO58` | ADIO5–8 current (ADIO6–8 0 A until ADC3) |
| base+4 | `PDM_REASONS` | 4-bit trip reason per channel + last ECU RPM |

Trip reason: 0 none, 1 fast short, 2 overcurrent, 3 sense fail.

## Bring-up

1. ECU verbose on, PDM consume off: SavvyCAN + DBC should show BASE0 pump/fan
   bits changing when the ECU commands those outputs.
2. Enable PDM consume: the Fuel Pump / Fan / Fan 2 pins follow the ECU.
   Unplug CAN: those outputs drop within 500 ms.
3. Enable status TX: `PDM_STATUS0` `EcuAlive` tracks the timeout;
   `HP1_A` tracks pump current after the e-fuse window.

Host tests (no toolchain): `firmware/run_pdm_can_logic_test.sh`.
