# 05 — Meter service, hardware and Zigbee adapters

This packet covers phases 5 and 7. The BL0937 driver, Telink counter routes,
meter construction and the measurement adapters are implemented for
`OUTLET_BSEED_TS011F_PM_DRZLXJNE`; the decisions are in
[STATE.md](./STATE.md#metering-design-bl0937-ts011f-drzlxjne). Hardware
counting, reference-meter calibration and power-cut loss remain open.

The exact outlet must be opened before a real profile or driver choice is
committed. Probe observations assist continuity tracing and polarity checks;
they do not establish IC identity, meter signal timing or calibration.

## Evidence classification

Confirmed for `_TZ3210_drzlxjne` by the
[exact-device report](https://github.com/sprut/Hub/issues/4866):

- TS011F router, manufacturer code 4417, endpoint 1 plus Green Power endpoint
  242;
- OnOff, Electrical Measurement and Metering server clusters;
- Basic application version 192 and hardware version 1;
- OTA manufacturer 4417, image type 54179 and OTA file version 82.

Application version 192 and OTA file version 82 are different attributes. Do not
name or select an OTA payload using the Basic application byte.

Confirmed only for the `_TZ3000_2uollq9d` sibling by the
[metering-fork commit](https://github.com/HobboRobin/tuya-zigbee-switch-with-metering/commit/bf2ee9296cc7251de07971becb8b437504e55e16):

| Function | Pin |
| --- | --- |
| network LED | C3 |
| local button | B5, pull-up |
| relay | D2 |
| relay indicator | B4 |
| CF | C0 |
| CF1 | C2 |
| SEL | C1 |

The sibling's layout does not apply to `drzlxjne`: the GPIO probe on the
owner's unit found button B4, relay B5, relay indicator C4 and BL0937 CF C2,
CF1 C3, SEL D2 (SEL high = voltage), giving
`drzlxjne;TS011F-BS-PM-3;SB4u;RB5;IC4;EBC2C3D2;M;`. Only the voltage scale
is measured on this unit; current and power scales come from the sibling.
The official BSEED product class is 16 A with
[energy monitoring](https://www.bseed.com/products/bseed-echo-zigbee-eu-standards-socket-with-energy-monitoring-smart-zigbee-electric-socket-work-with-alexa-google-assitant),
but that is not a pinout or protection calibration source.

## Reuse boundary from the metering fork

Reuse after tests:

- Telink timer0/timer1 GPIO-trigger counter setup and resource limit of two;
- stub pulse injection and SEL-polarity tests;
- HLW8012/BL0937 pulse conversion, SEL settling, timeout/plausibility checks and
  fixed-point remainder math;
- standard Electrical Measurement/Metering ids, types and multiplier/divisor
  tests;
- factory-default-versus-NVM calibration precedence tests.

Do not port:

- stop/read/reset/start counter sampling, which creates a pulse-loss window;
- raw CF/CF1/SEL fields in the normalized meter sample;
- driver-owned accumulated Wh as the authoritative total;
- raw `hal_tasks` scheduling in drivers;
- parser-triggered meter/cluster initialization;
- calibration, energy or overload NVM owned by clusters;
- Electrical Measurement calling `relay_cluster_off/on`;
- the fork's Silabs GPIO-counter no-op.

## Phase 5: counter HAL and meter service

### Current software and remaining gate

The working tree contains `hal/gpio_counter.h`, stub capture, a fake meter
driver, `meter_service`, owner-record clearing at NVM version 8, composition
counter-route preflight and native regression tests. Telink and Silabs return
zero counter capacity and reject routes. `feature_wiring` does not yet construct
meter instances. Software validation is recorded as hand-off evidence in
`STATE.md`; it does not close hardware calibration or checkpoint endurance.

Use Phase 6's GPIO manager for every driver pin, including CF/CF1/SEL or serial
signals. Pin claims and counter-route reservations are separate requirements.
Probes cannot borrow a running meter's pins/resources; a GPIO edge observation
is not a replacement for a cumulative hardware counter or an oscilloscope.

### Counter contract

Put the API in `src/hal/gpio_counter.h`, not the button-oriented GPIO header:

```c
typedef int8_t hal_gpio_counter_id_t;

typedef struct {
    uint32_t total;
    uint32_t timestamp_ms;
    uint32_t overflow_count;
    uint32_t error_count;
    uint8_t running;
} hal_gpio_counter_snapshot_t;

hal_gpio_counter_id_t hal_gpio_counter_open(
    hal_gpio_pin_t pin, hal_gpio_counter_edge_t edge, hal_gpio_pull_t pull);
bool hal_gpio_counter_snapshot(
    hal_gpio_counter_id_t id, hal_gpio_counter_snapshot_t *out);
void hal_gpio_counter_close(hal_gpio_counter_id_t id);
uint8_t hal_gpio_counter_capacity(void);
bool hal_gpio_counter_can_route(uint8_t slot, hal_gpio_pin_t pin);
```

Snapshots are atomic and non-destructive. Drivers subtract successive unsigned
totals. Platform code extends hardware wrap and increments diagnostics. Telink
reserves timer0/1; watchdog timer2 is not borrowed. Composition rejects a third
counter or a pin for which `can_route` is false. The capacity/route queries have
no hardware side effects and are used by composition preflight. Silabs must have
a real implementation or reject the profile at preflight—successful no-op
initialization is forbidden.

### Driver contract

Implement the tagged `energy_meter.h` interface and normalized `meter_sample_t`
from the architecture. Driver contexts own pins, counters/serial state,
chip-specific constants, SEL phase and raw diagnostics. They do not own NVM,
Zigbee, protection or authoritative accumulated energy.

The first real pulse driver is named for the confirmed IC. Shared HLW8012/BL0937
math may live in one private helper, but the tag and SEL polarity remain explicit
so a profile cannot silently select the wrong chip.

Sampling uses `timer_service`. A sample validity mask is per quantity. SEL mode
switching discards the first unsettled CF1 window. CF energy delta is reported in
micro-watt-hours with remainder retained in the driver so no fractional pulses
are lost.

### Service and persistence

`meter_service` owns two fixed instances, service-level gain calibration,
latest samples, accumulated `uint64_t` micro-watt-hours, subscribers and NVM.
Priority is runtime gain override, profile default gain, then driver unity.

Energy checkpointing must satisfy both gates before constants are fixed:

1. a documented 10-year platform write-budget calculation using the actual NVM
   wear-level behavior;
2. measured worst-case energy loss after abrupt power cut at representative and
   maximum load.

The service never writes per pulse/sample. Reset energy updates RAM, driver
remainder and the service record as one operation. Calibration/reset work with no
Zigbee adapter instantiated.

## Phase 7: Zigbee adapters

Electrical Measurement reads one meter snapshot and exposes only valid values.
Use standard wire units and declared multipliers/divisors; saturate rather than
wrap. Driver diagnostics, when exposed, remain custom read-only attributes and
do not enter `meter_sample_t`.

Metering projects the same service energy total to `uint48`. It owns no counter.
Reset-energy and calibration writes validate payload/range, call the service,
then refresh adapter mirrors. Both adapters subscribe to service updates and
report on the endpoint selected by `endpoint_config_t`.

Support multiple instances internally even though the first BSEED profile has
one. Endpoint trampolines resolve by endpoint/meter id; no singleton global
cluster pointer is allowed.

Register standard clusters in Telink, Silabs and stub using Phase 6 metadata,
explicit access flags, command context and per-cluster candidate-write callbacks.
Put cluster IDs in `zcl_ids.h` and attributes/commands in measurement-owned
definition headers. Update platform ZCL tables, generator templates and converter
tests; generated Z2M/ZHA/HOMEd artifacts are a separate commit. Calibration and
energy reset remain feature-specific; global counters and GPIO operations stay
in Device Diagnostics. Do not add meter internals to Basic or Device Config.

## Required tests

Native:

- cumulative counter snapshots, simultaneous CF/CF1, wrap extension, resource
  exhaustion, invalid handle and no pulse loss across snapshots;
- pulse conversion at zero/low/nominal/high/glitch rates, timeout, SEL inversion
  and settling;
- validity masks, normalized gain bounds, 64-bit energy/remainder, checkpoint
  scheduling, NVM failure and reset;
- adapter min/max/saturation, multiplier/divisor and malformed-write tables.

- meter ownership conflicts with probes and unavailable counter/serial routes;
  capability failures leave unrelated feature pins and resources unchanged.

Stub/pytest:

- meter pulses do not change edge/button diagnostics;
- measurements and energy continue during Zigbee outage and without a relay;
- reports resume from current service state without replaying samples;
- runtime calibration overrides profile defaults across reboot;
- reset energy has one owner and no second cluster counter;
- meter failure leaves relay/button operation available.

Hardware before unblocking Phase 5:

- photograph IC markings and both PCB sides with mains disconnected;
- continuity-check candidate C3/B5/D2/B4/C0/C2/C1 roles;
- use a management-only configuration on the known MCU/module; verify button
  inputs and allowed relay/LED pulses at isolated current-limited low voltage
  through Phase 6 diagnostics before a metering build. Keep config and
  descriptors unchanged between probe sessions;
- scope CF/CF1/SEL with an isolated setup, confirm edge polarity and IC tag;
- compare voltage/current/power at no load and at least three resistive loads;
- assign profile calibration only from the exact unit/revision evidence.

## Completion and hand-off

Phase 8 starts only after meter samples, energy and calibration have exactly one
owner and both Zigbee clusters are projections using the Phase 6 HAL. Record
unsupported platform counter routes explicitly; do not mark Phase 5 done on the
strength of the candidate sibling pinout.
