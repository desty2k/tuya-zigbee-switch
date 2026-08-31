# 07 — Startup input settle gate

Phase 2, priority 1. This is the power-cycle regression path for upstream issue
460.

## Failure model

The BSEED issue reports relay and binding actions during power-up, with behavior
depending on relay mode. That points to input transitions during GPIO/pull and
touch-controller stabilization rather than relay startup policy alone.
`boot_press` suppresses gesture generation for a button already down at input
initialization, but it does not quarantine edges that arrive shortly afterward;
raw `DOWN`/`UP` consumers can still change relays or send bindings.

## Gate semantics

```c
#define BUTTON_STARTUP_SETTLE_MS 250
```

The button input layer has `STARTUP_SETTLING` and `RUNNING` states.

During `STARTUP_SETTLING`:

- GPIO capture and loss accounting remain active;
- edges may enter the ring but do not reach debounce, gesture, relay, binding,
  indicator-feedback or Zigbee event consumers;
- each suppressed edge increments `startup_edges_suppressed`;
- relay startup behavior is applied exactly once by the relay controller and is
  not inferred from button levels.

At the settle deadline:

1. drain and discard pre-deadline queued edges;
2. sample every configured input through `hal_gpio_read()`;
3. seed raw and stable state from that snapshot;
4. reset debounce timestamps and transient press identity;
5. mark any sampled-down input as a boot press, suppressing it until a stable
   release;
6. enter `RUNNING` and process only later edges.

No startup edge is replayed as an action. Durable relay state comes from
`StartUpOnOff`, not from a guessed startup gesture.

## Timing and scope

The initial 250 ms is a design value, finalized by oscilloscope or logic-analyzer
capture across cold starts and short breaker interruptions. It is global and
volatile; it adds no config token or NVM item. The same semantics apply to all
button types because a boot transient must not override explicit power-on
behavior.

The timer begins after GPIO input initialization and before watched interrupts
can feed user actions. A reset occurring before the deadline simply starts a new
settle interval.

## Interaction with real user input

A user-held button at the deadline is treated as a boot press: no `DOWN`, hold,
relay or binding action occurs. Its eventual stable `UP` restores the released
baseline without emitting a click. A complete press that begins and ends inside
the quarantine is intentionally ignored.

This conservative rule favors deterministic power-on state over accepting input
during the first quarter second after mains restoration.

## Diagnostics

| Counter | Meaning |
| --- | --- |
| `startup_settle_count` | settle windows entered since boot; normally one |
| `startup_edges_suppressed` | captured transitions quarantined |
| `startup_inputs_down` | inputs sampled down at the settle deadline |

## Acceptance

For each `StartUpOnOff` mode (`OFF`, `ON`, `TOGGLE`, `PREVIOUS`) and each relevant
relay mode, perform at least 50 breaker cycles with buttons released and 10 with
one button held:

- final relay state matches startup policy;
- no binding command or `0xFC02` action is emitted before the first post-settle
  physical press;
- all four relay endpoints agree with physical relay state;
- startup suppression counters explain any measured input transient;
- the first ordinary post-settle press works once.

Reference: [upstream issue 460](https://github.com/romasku/tuya-zigbee-switch/issues/460).

