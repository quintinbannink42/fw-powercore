# PowerCore — firmware identity (trial)

| Field | Value | Notes |
|-------|-------|-------|
| Product | **PowerCore** | Standalone PDM (not an engine ECU) |
| `SHORT_BOARD_NAME` | `powercore` | Lower-case for TunerStudio auto `.ini` download |
| `FIRMWARE_ID` | `powercore` | Must stay in sync with short name |
| Fallback only | `powercorea` | If hellen-one / rusEFI rejects `powercore` (no dashes/underscores). Do not invent a third name. |
| Repo stub path | `fw-hellen-pdm-razor` | Prefer `fw-powercore` when remotes are created |
| Former names | hellen-pdm-razor, Hellen Razor-class PDM | Docs/history only |
| KiCad hardware | `pdmrazora` | Jeoff; hellen-one-safe tokens — **do not rename copper here** |

## Constraints TBD

- hellen-one / rusEFI board-name rules: validate `powercore` in FW-0 compile / registry; escalate to Boss if rejected → try `powercorea`.
- TunerStudio environment must be **PDM-shaped** (channels, current, trips, CAN consume) — not a full ECU page set.

Owner: Code Jeoff · Reports to: Boss · Locked brief: `../POWERCORE_BRIEF.md`
