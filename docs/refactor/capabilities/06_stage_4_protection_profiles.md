# 06 — Protection, BSEED profiles and release

This packet implements phases 8 and 9. It turns measurements into safe policy,
then promotes only hardware-confirmed facts into device profiles.
It depends on Phase 6 management/GPIO ownership and Phase 7 measurement adapters.

## Phase 8: overload and indicator relations

### Protection configuration

Separate immutable ratings from user settings:

```c
typedef struct {
    uint8_t  meter_id;
    uint16_t protected_relay_mask;
    uint32_t hard_power_mw;
    uint32_t hard_current_ma;
    uint32_t hard_overvoltage_mv;
    uint32_t hard_undervoltage_mv;
    uint32_t meter_failure_ms;
    uint8_t  fail_closed;
} overload_config_t;

typedef struct {
    uint32_t power_limit_mw;       /* 0 disables user limit */
    uint32_t current_limit_ma;     /* 0 disables user limit */
    uint32_t overvoltage_mv;       /* 0 disables user limit */
    uint32_t undervoltage_mv;      /* 0 disables user limit */
    uint32_t trip_delay_ms;
    uint32_t reconnect_delay_ms;
    uint8_t  auto_clear_user_trip;
} overload_settings_t;
```

Every non-zero user upper limit is clamped at or below its hard rating. A user
undervoltage threshold is clamped at or above the hard minimum. Hard limits and
fail policy never come from writable attributes or the persisted settings
record.

### State machine and relay interaction

Use explicit states and one current cause:

```text
ARMED → TRIP_PENDING → TRIPPED → CLEAR_PENDING → ARMED
```

- A hard current/power/voltage violation trips immediately.
- A user violation must remain continuous for `trip_delay_ms`; a valid safe
  sample cancels TRIP_PENDING.
- Trip first acquires its assigned relay inhibit id, then submits PROTECTION OFF
  to every bit in the relay mask.
- TRIPPED is persisted before it can be externally cleared.
- Hard trips require explicit clear. A clear request enters CLEAR_PENDING and
  succeeds only after all required quantities are valid/safe for
  `reconnect_delay_ms`.
- User trips auto-enter CLEAR_PENDING only when enabled. Release removes the
  inhibit and does not submit ON.
- Invalid samples never clear a trip. After `meter_failure_ms`, apply the
  immutable fail-open/fail-closed profile policy.

Do not port the metering fork's automatic TURN_ON/startup-mode retry behavior.
Startup state cannot override a persisted protection inhibit.

One controller protects a relay mask, including all four gangs or both cover
motor relays. Multiple controllers may overlap; relay inhibit owner bits ensure
one controller cannot release another's trip.

Protection settings use the relevant feature adapter and Phase 6 per-cluster
write/service contracts. They do not move into global Device Config or Basic.
Configured protected relay pins remain claimed by relay drivers, so Diagnostics
cannot pulse around a protection inhibit. Test that rejection while tripped.

### Indicator relations

Add base-mode sources, not new LED owners:

- relay follow/inverse uses applied relay state;
- cover moving/direction uses cover snapshots;
- meter/protection error uses meter validity and trip state.

`indicator_feedback` resolves these below disconnected-network,
commissioning/reset and transient feedback priorities already tested. Domain
modules publish state; they never call LED primitives.

### Phase 8 tests

Native tests cover hard versus delayed user trips, threshold clamps, hysteresis,
invalid/intermittent samples, meter-failure policy, relay masks, overlapping
inhibitors, persisted reboot state, manual/auto clear and millis wrap.

Stub tests attempt BUTTON, GESTURE, ZIGBEE, TIMER, STARTUP and COVER ON while
tripped. Every attempt is BLOCKED; OFF remains accepted. Clear never re-energizes
a relay. Unprotected relays and local input continue operating.

## Phase 9: promote hardware evidence to profiles

### Candidate versus confirmed data

Research/candidate values remain in this document and `STATE.md`. A committed
`device_db.yaml` profile contains only values confirmed for the exact PCB
revision. The profile generator normalizes into `device_composition_t`; it does
not construct services or endpoints.

Planned keys:

```text
OUTLET_BSEED_PM_TS011F_DRZLXJNE
SWITCH_BSEED_TS130F_VSJ5O7DM
```

The outlet candidate is C3 status LED, B5 button, D2 relay, B4 relay indicator,
C0 CF, C2 CF1 and C1 SEL. The IC, PWM support, polarity and calibration remain
unknown until bench confirmation. The cover requires a complete trace of stop,
up, down, both relay drivers and every backlight/status channel.

### Disconnected-board discovery

For each unopened device:

1. record labels, PCB revision, module and metering/driver IC markings;
2. photograph both sides before removing components;
3. continuity-trace module GPIO to button/touch circuits, transistor inputs,
   relay coils, LEDs and meter signals with mains disconnected;
4. verify no GPIO is shared incompatibly and establish active polarity;
5. provision the known MCU/module once with management-only firmware/config;
   use Phase 6 input observation and permitted bounded pulses at current-limited
   isolated low voltage for GPIO/polarity confirmation. Record pin ownership,
   capability map and command/state observations; repeated probes do not change
   configuration, reboot or require coordinator re-interview;
6. only after tracing and GPIO confirmation install the characterized feature
   profile and run no-load mains tests with an isolated programmer/test setup.

Do not pulse untraced motor/relay circuitry with mains or a motor attached.
Probe-safe platform GPIO is not evidence that its unknown load is safe. Profiles
may narrow the module's probe allowlist and must claim every configured I/O,
including meter and ADC/PWM/serial resources. Final feature-profile installation
may require a coordinator descriptor refresh; GPIO probe sessions do not.

The cover trace must also determine whether STOP is a dedicated input or a
simultaneous-contact condition. The action mapping differs; motor control does
not.

### Profile and schema fields

Add YAML capability objects only after the first exact profiles are known. The
schema must distinguish:

- hardware pins, driver tag/constants, ratings, relay relationships, endpoint
  attachment and default mapping;
- runtime defaults for travel, dead zones, indicators, calibration gains and
  user protection settings;
- OTA manufacturer/image ids and role, which remain build metadata.

Schema fixtures cover meter-only, metered outlet, N-gang metered switch, cover
and metered cover. A generator test compares their normalized graphs with
equivalent runtime strings wherever their tokens can represent the same device.
Every composition includes the same EP1 management clusters; optional attributes
and opt-in diagnostic exposes follow build/backend capabilities. Profile
generation must not manufacture a normal switch entity for a GPIO probe, expose
absent hardware controls or add management attributes to Basic. Small-device
profiles retain normal functionality without optional counter/probe facilities.

### OTA and persisted-state gate

Keep `SWITCH_BSEED_TS0726_4GANG` image type 43627. The exact outlet stock report
shows manufacturer 4417/image type 54179; allocate a new unique firmware image
type only after checking all expanded database identities. Capture the cover's
stock OTA query before assigning its firmware type.

For each device build normal, from-stock and forced images as applicable. Parse
the produced OTA headers and assert manufacturer, image type, file version and
role. Perform an OTA installation from the supported source image and verify
changed records reset to safe defaults, current owner records load correctly,
local control and network rejoin work, and no probe resumes on boot. No retained
record translation or compatibility aliases are required.

## Full hardware release matrix

| Target | Minimum pass evidence |
| --- | --- |
| TS0726 switch | 100 taps/gang, simultaneous gangs, detached/binding, indicator, breaker cycles, OTA |
| TS011F outlet | fixed-topology diagnostic discovery, pin/IC proof, three-point calibration, no-load stability, 24 h energy comparison, meter outage, overload at safe test load, breaker cycles, OTA |
| TS130F cover | fixed-topology diagnostic discovery, pin proof, 100 reversals, STOP in dead time, manual timeout, unequal times, 0/25/50/75/100 GOTO, dead zones, overrun, power loss during motion, OTA |

Never create an overload test by exceeding the product or test-load rating.
Validate trip logic first with scaled injected samples and use hardware loads
within a documented safe setup.

## Completion

The architecture is release-ready only when:

- phases 1–9 are done in `STATE.md` with commits and concise evidence;
- source and generated changes are in separate commits;
- full tests, format, Telink target builds and applicable Silabs builds pass in
  the required Colima profile;
- OTA identities are unique and the tested installation paths work;
- Phase 6 platform input/output, cutoff/cleanup and coordinator gates have
  evidence; unsupported GPIO operations return explicit errors;
- small-target budgets and regression evidence cover flash/OTA, RAM/stack,
  descriptor/timer/reporting resources, local responsiveness and idle sleep;
  optional diagnostics cannot displace required functionality or break a supported
  device. The normal product UI stays small and diagnostics are opt-in;
- Basic is identity-only, management metadata is consistent across backends and
  integrations, and all probes leave configuration/NVM/descriptors unchanged;
- all three BSEED hardware rows pass, or an unsupported platform/device is
  explicitly excluded rather than marked complete;
- large logs/captures remain external with stable references in the evidence log.
