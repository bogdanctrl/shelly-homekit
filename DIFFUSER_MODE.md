# Shelly Plus Uni – essential-oil diffuser mode

This fork adds an optional, model-specific diffuser mode for **Shelly Plus Uni**.
It is enabled only for the `ShellyPlusUni` build and does not change the legacy
Shelly UNI target.

## GPIO assignment

| Function | Shelly Plus Uni GPIO | Firmware use |
|---|---:|---|
| OUT1 | 19 | Main button contact; used for ON and OFF |
| OUT2 | 21 | LED-button contact; used together with OUT1 for OFF |
| IN1 | 37 | Not used by diffuser state logic |
| IN2 | 38 | Atomizer feedback; authoritative ON/OFF state |

The input is configured active-high by default (`on_value = 1`). The existing
`sw1.in_inverted` configuration option can invert the feedback polarity.

## HomeKit behavior

The diffuser is exposed as one HomeKit Switch accessory. Its `On` characteristic
is always read from IN2. A write to `On` does not directly set the output state:

- ON request while IN2 is OFF: pulse OUT1 active for 100 ms; OUT2 stays inactive.
- OFF request while IN2 is ON: pulse OUT1 and OUT2 active simultaneously for
  3000 ms. The diffuser stops only when both button contacts are held closed
  for the full three seconds.
- If IN2 already matches the requested state: do not pulse either output.
- Repeated writes while awaiting feedback do not produce extra pulses.
- The request remains pending for at most 15 seconds. If IN2 does not confirm
  the requested state, the component logs a warning and continues reporting
  the actual IN2 state.
- Every IN2 state transition raises a HomeKit notification.

Both outputs are returned to their inactive state after each pulse. OUT2 is
not a standalone LED indicator in this mode: it simulates the diffuser's
second (LED) button and is required together with OUT1 for the OFF sequence.
The firmware never uses either output's current level as the reported diffuser
state.

## Build

The existing GitHub Actions matrix already includes `ShellyPlusUni`; pushing the
fork changes will cause that target to be built by the repository workflow.
