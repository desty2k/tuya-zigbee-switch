# 01 — Architecture

## Goal

The hardened firmware preserves the input/relay architecture while adding
explicit safety boundaries around interrupt dispatch, Zigbee transmission,
fixed resources, network recovery, startup and persistence.

```text
TLSR8258 GPIO IRQ
    │ snapshot + timestamp + SPSC push + pending flags only
    ▼
normal-context poll
    ├── GPIO re-arm/reconciliation
    └── button worker scheduling
            │
            ▼
      input / gesture / relay pipeline
            │
            ├── durable state ──► staggered report/resync service
            └── immutable event ► paced 0xFC02 TX pump

network monitor ──► transition counters, rejoin resync, announce retry
resource builder ─► validated endpoint/cluster/attribute graph ─► Telink ZCL
NVM commit service ► delayed per-item snapshots + failure counters
```

## Scope boundary

The design is optimized for the database entry
`SWITCH_BSEED_TS0726_4GANG`: four pull-up momentary inputs, four relays, eight
router endpoints and a TLSR8258. Generic fixes stay in shared layers where doing
so reduces risk, but the release evidence is the Telink router build and this
physical board.

## Code findings and design routing

| # | Current code finding | Hardening owner |
| --- | --- | --- |
| 1 | `src/telink/hal/gpio_interrupts.c` posts the GPIO dispatch task from `gpio_isr_callback()`, while the ISR-invoked sink in `button_input.c` posts another task | [02_isr_task_isolation.md](./02_isr_task_isolation.md) |
| 2 | `button_event_cluster_drain()` removes all entries in a loop; Telink ignores the `zcl_sendCmd()` result and returns success | [03_button_event_transport.md](./03_button_event_transport.md) |
| 3 | Several write/command trampolines directly index endpoint tables; `B` and `X` polarity is fixed low while `S` derives it separately from token text | [04_zigbee_write_safety_and_polarity.md](./04_zigbee_write_safety_and_polarity.md) |
| 4 | Cluster modules append through unchecked `cluster_count`; Telink writes fixed input/output cluster and attribute buffers without aggregate preflight; dispatch tables use magic `[10]` sizes | [05_resource_bounds.md](./05_resource_bounds.md) |
| 5 | Telink exposes little evidence for intermittent router loss; boot announce is marked sent without checking its result | [06_router_liveness.md](./06_router_liveness.md) |
| 6 | `boot_press` covers the sampled boot level but does not quarantine transitions during post-GPIO startup settling | [07_startup_input_settle.md](./07_startup_input_settle.md) |
| 7 | joined callbacks drain button events and update indicators, but do not deliberately snapshot and pace all relay/button state after recovery | [08_rejoin_resynchronization.md](./08_rejoin_resynchronization.md) |
| 8 | edge overflow is counted, but the button FSM is not resampled after lost history | [09_gpio_overflow_reconciliation.md](./09_gpio_overflow_reconciliation.md) |
| 9 | the database has no enforcing image-type collision check in test CI; target id `43627` is currently unique | [10_ota_image_type_validation.md](./10_ota_image_type_validation.md) |
| 10 | cluster callbacks synchronously write NVM and usually ignore failure status | [11_nvm_write_coalescing.md](./11_nvm_write_coalescing.md) |

## New and changed responsibilities

| Responsibility | Owner | Contract |
| --- | --- | --- |
| IRQ capture | Telink GPIO HAL | No generic task/timer or Zigbee calls from ISR |
| Normal-context GPIO service | GPIO HAL + `button_input` poll hooks | Consume pending flags, re-arm hardware, schedule workers |
| Event transport | `button_event_cluster` | Bounded FIFO, one paced attempt, retry/TTL accounting |
| Descriptor construction | shared Zigbee builder helpers | Capacity checked before every append |
| Platform registration | Telink Zigbee HAL | Preflight aggregate counts; all-or-nothing registration |
| Network monitoring | application/network service | Edge-triggered state transitions and diagnostic counters |
| Recovery resync | Zigbee resync service | Stagger current state only; never replay actions |
| Startup quarantine | `button_input` | Suppress boot transients and seed from settled GPIO |
| Overflow recovery | `button_input` + GPIO HAL | Detect loss and converge logical state to a fresh snapshot |
| Persistence | NVM commit service | One owner and delayed snapshot per dirty item |

## Execution contexts

| Context | Allowed | Forbidden |
| --- | --- | --- |
| GPIO IRQ | read GPIO registers, timestamp, update IRQ-owned state/counters, SPSC push, set pending flag, mask GPIO IRQ | task post/cancel, ZCL/Zigbee calls, NVM, debounce/gesture/relay policy |
| Main superloop / app poll | exchange pending flags, re-arm GPIO, schedule due work, monitor network status | unbounded loops or synchronous retry storms |
| HAL task callback | debounce, gesture, relay deadlines, one TX attempt, one resync step, one NVM commit | draining an unbounded Zigbee queue |
| Zigbee callback | validate input, mutate RAM state, mark NVM dirty, enqueue response work | unchecked endpoint indexing, synchronous flash churn |

The main loop remains watchdog-friendly. Every new pump performs bounded work per
invocation and returns to `tl_zbTaskProcedure()`.

## Failure containment

1. An edge-ring overflow cannot silently leave a button permanently down.
2. A Zigbee send rejection cannot discard the front event.
3. An expired event cannot block newer entries.
4. Invalid endpoint or capacity input cannot corrupt adjacent memory.
5. A failed announce cannot be marked successful.
6. A burst of attribute writes cannot produce an equal burst of flash writes.
7. Rejoin reports cannot flood all eight endpoints in one loop iteration.

## Diagnostics model

Counters are monotonic `uint32_t` values reset on reboot unless explicitly noted.
The Basic cluster and stub `diag` output expose the same snapshot.

| Domain | Counters / values |
| --- | --- |
| GPIO | IRQs, captured, dropped, rearm-limit, reconciliations |
| Button TX | queue dropped, expired, send failed, queue high-water, submitted |
| Zigbee resources | descriptor validation failures and failing resource kind |
| Network | status transitions, losses, rejoins, steering attempts/failures, announce attempts/failures |
| Runtime | uptime, reset cause, watchdog-reset count when available |
| Startup | settle windows, suppressed edges, startup-down inputs |
| NVM | commits, coalesced updates, write failures, dirty items |

New diagnostic attribute ids are allocated together and mirrored in generator
templates. Existing ids `0xff10` through `0xff15` retain their meanings.

## Board capacity invariant

The BSEED 4-gang consumes all eight Telink endpoint slots. Therefore:

- endpoint count `8` is valid and `9` is rejected;
- endpoint ids are validated independently from endpoint count;
- lookup storage includes every valid endpoint id plus slot zero;
- aggregate cluster/attribute counts are calculated before pointer writes;
- adding a cluster to this board requires a capacity-budget test, not an assumed
  spare slot.

## Non-goals

Hardening does not add user-visible endpoints, clusters, actions or configuration
tokens except read-only diagnostics. It does not make volatile button events
durable, reconstruct missed gestures, or guarantee RF delivery without a stack
confirmation primitive.
