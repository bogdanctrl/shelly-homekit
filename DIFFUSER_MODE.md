# Shelly Plus Uni – essential-oil diffuser mode

This fork adds an optional, model-specific diffuser mode for **Shelly Plus Uni**.
It is enabled only for the `ShellyPlusUni` build and does not change the legacy
Shelly UNI target.

## GPIO assignment

| Function | Shelly Plus Uni GPIO | Firmware use |
|---|---:|---|
| OUT1 | 19 | Single ON/OFF button contact pair |
| OUT2 | 21 | Not used by diffuser mode |
| IN1 | 37 | Not used by diffuser state logic |
| IN2 | 38 | Atomizer feedback; authoritative ON/OFF state |

The input is configured active-high by default (`on_value = 1`). The existing
`sw1.in_inverted` configuration option can invert the feedback polarity.

## HomeKit behavior

The diffuser is exposed as one HomeKit Switch accessory. Its `On` characteristic
is always read from IN2. A write to `On` does not directly set the output state:

- ON request while IN2 is OFF: activate OUT1 for 100 ms.
- OFF request while IN2 is ON: activate the same OUT1 for 3000 ms.
- If IN2 already matches the requested state: do not pulse the output.
- Repeated writes while awaiting feedback do not produce extra pulses.
- The request remains pending for at most 15 seconds. If IN2 does not confirm
  the requested state, the component logs a warning and continues reporting
  the actual IN2 state.
- Every IN2 state transition raises a HomeKit notification.

OUT1 is returned to its inactive state after each press. OUT2 is not configured
or used by this diffuser mode. The firmware never uses the output level as the
reported diffuser state.

## Build

The existing GitHub Actions matrix already includes `ShellyPlusUni`; pushing the
fork changes will cause that target to be built by the repository workflow.
