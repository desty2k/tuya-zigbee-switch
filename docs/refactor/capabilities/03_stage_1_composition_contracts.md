# 03 — Stage 1: composition and capability contracts

This is the execution packet for phases 1 and 2. It may be completed in one
implementation run, but Phase 1 must be green before changing relay/action
ownership. The target rules remain authoritative in
[01_architecture.md](./01_architecture.md).

## Required contracts

- parsing produces one validated, immutable `device_composition_t` without side
  effects;
- `feature_wiring` is the only composition root;
- generic controllers expose no Zigbee types;
- relay state fan-out and protection inhibits are structurally available;
- `action_mapper` owns local mapping policy;
- relay, mapping and indicator NVM records belong to their feature owners.

## Phase 1: normalized composition root

### 1. Define configuration-only types

Add `device_composition.h/.c`. Hardware configuration structs contain pins,
polarity, pulls, ids and immutable relationships only. Driver runtime structs,
timers, callbacks, attribute storage and NVM buffers do not appear in the parsed
model.

Keep the bounds in one header and reuse them for arrays, validation and NVM
ranges. The initial values match the architecture: 10 relays, 11 buttons, five
indicators, three covers, two meters/protections and 10 endpoints.

```c
typedef enum {
    CONFIG_PARSE_OK,
    CONFIG_PARSE_SYNTAX,
    CONFIG_PARSE_LIMIT,
    CONFIG_PARSE_REFERENCE,
    CONFIG_PARSE_PIN_CONFLICT,
    CONFIG_PARSE_RESOURCE,
} config_parse_result_t;

config_parse_result_t config_parser_parse(
    const char *text, device_composition_t *out);
```

On failure, leave `out` zeroed and return a reason. The parser never calls reset,
GPIO, PWM, NVM, task/timer or Zigbee functions and never edits the source string.

### 2. Define runtime token semantics

Move token decoding in small slices and lock each with table-driven tests. Keep
manufacturer/model extraction, endpoint ordering and every currently accepted
token. Do not introduce meter/profile YAML fields in this stage.

Validation occurs after syntax parsing and before construction:

- counts fit their declared bounds;
- every relay/button/indicator/cover/action reference exists;
- a pin has one incompatible hardware role at most;
- cover relays are distinct and belong to no second cover;
- endpoint ids and cluster/attribute capacities fit platform limits;
- interlock masks reference existing relays;
- the build-provided role/image identity cannot be changed by parsed input.

### 3. Move construction to `feature_wiring`

Split build from start so tests can inspect the graph before tasks or Zigbee run:

```c
feature_wiring_result_t feature_wiring_build(
    const device_composition_t *composition);
void feature_wiring_start(void);
```

`build` creates safe relay drivers/controllers, interlocks, indicators, buttons,
gestures, mappings, domain capabilities and endpoints in dependency order.
`start` applies restored startup policy, enables input/service timers and invokes
Zigbee initialization. Network join is not required for local behavior.

Delete construction globals from `config_parser.c` as their replacement lands.
Initialization belongs only in the composition root.

Phase 6 extends this root with always-present EP1 management clusters, recovery
from invalid feature configuration, GPIO owner claims and shared edge routing.
Composition preflight remains side-effect free; actual claims are acquired as a
bounded transaction before hardware construction. Cover motor pins stay owned
by their relay drivers, and meter/battery/PWM/serial pins participate in claims.

## Phase 2: relay and action contracts

### 1. Make relay control protocol-independent

Remove `hal/zigbee.h` from the relay controller header. Add the result/source
types from the architecture and map them to ZCL statuses only in
`relay_cluster.c`.

Use four fixed observer slots per relay. This covers the relay adapter, cover
controller, indicator relation and one diagnostic/future consumer without
allocation.

```c
bool relay_ctrl_subscribe(uint8_t relay_id, relay_state_cb_t callback,
                          void *context);
relay_result_t relay_ctrl_submit(const relay_request_t *request);
```

Observers receive `(relay_id, is_on, source)` after the driver was applied. A
deferred request produces no notification until interlock activation. Duplicate
state requests return UNCHANGED and do not notify.

Give `feature_wiring` a fixed pool of inhibit ids. `relay_ctrl_inhibit(mask, id)`
is idempotent; `relay_ctrl_release(mask, id)` removes only that owner's bit. OFF
always works. Any ON-like request while any bit remains returns BLOCKED.

### 2. Move local policy to `action_mapper`

Represent the trigger and target explicitly:

```c
typedef struct {
    uint8_t button_id;
    action_trigger_type_t trigger;
    uint8_t count;
} action_trigger_t;

typedef struct {
    action_type_t type;
    uint8_t target_id;
    uint16_t argument;
} action_target_t;
```

The mapping table is fixed-capacity and validated at wiring time. Immediate
DOWN/UP and normalized gestures enter one dispatcher. Targets cover relay,
cover, binding, button event and system action. Switch/cover-switch Zigbee
adapters read or change mappings through APIs; they do not execute local relay
or cover calls.

Binding is an action executor implemented beside the Zigbee adapter. The mapping
core identifies the binding action but does not format frames.

### 3. Move persistent ownership

Allocate new NVM ids; do not reuse cluster-owned ids with a new layout.

| Setting | Owner record | Layout policy |
| --- | --- | --- |
| startup, previous state, inching, interlock | relay state/config record | current layout only; safe defaults on mismatch |
| indicator mode/state | indicator record | keyed by indicator id; reset on layout change |
| local/detached, relay target, binding, gestures | action/input record | reset on layout change |
| cover target, reversal, local/binding mode | action/input record | reset on layout change |

Delete cluster-owned runtime writes and record translators. Bump
`NVM_MIGRATIONS_VERSION` for persisted layout/meaning changes and explicitly
clear replaced records. A clearing/load failure uses safe defaults and records
a diagnostic; it must not energize a relay. Phase 6.2 closes the outstanding
conversion-only paths identified in `STATE.md`.

## Required tests

Native:

- parser success/error tables and a fake HAL that fails if called;
- count/reference/pin/resource boundary and sentinel tests;
- relay result, observer order/capacity, deferred notification, inhibit owner
  isolation and millis-wrap tests;
- action trigger/target tables with fake relay, cover, binding and system sinks;
- fixtures for current owner layouts, invalid sizes, clearing and safe defaults.

Stub/pytest:

- endpoint/cluster graph snapshots for switch, relay, cover, relay-less and empty
  configurations, including management-only EP1 under Phase 6;
- all current switch, cover-switch, relay, interlock, latching, indicator,
  detached, binding and reset suites;
- reboot tests for current startup, previous state, mapping and indicator records;
- invalid composition fails before the first GPIO write or scheduled task.

## Completion and hand-off

Record Phase 1 and Phase 2 independently in `STATE.md`. Before Stage 2 starts:

- searches find no HAL/Zigbee/init call in `config_parser.c`;
- no base-component public header includes `hal/zigbee.h`;
- direct local relay/cover execution is absent from switch configuration modules;
- cluster-owned NVM ids have no runtime writer;
- full tests, format and TS0726 Colima build pass with filtered logs.
