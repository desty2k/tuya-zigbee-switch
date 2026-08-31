# 12 — Verification

Hardening uses four evidence levels:

1. native C tests for queues, race boundaries and state machines;
2. stub/pytest integration tests with virtual time and injected failures;
3. Telink build and static call/resource audits;
4. BSEED 4-gang hardware regression and multi-day soak.

The full existing suite remains required. Wall-clock sleeps are not used in
native or stub tests.

## Phase 1 native tests

### ISR isolation

- IRQ capture pushes multiple edges and only sets pending flags.
- Fake task functions fail the test if called while `fake_in_isr` is true.
- IRQ arrival during pending-flag claim is serviced by a later poll.
- Coalesced work notifications preserve every ring entry.
- GPIO re-arm runs before button worker scheduling.

### Safe dispatch and polarity

- Endpoint ids `0`, every valid boundary, boundary+1 and `255` are passed to
  attribute and command trampolines.
- Missing endpoints cause no mutation and commands return skipped.
- `B`, `S` and two-pin `X` inputs are tested with pull-up, 1 MΩ pull-up and
  pull-down.
- Mode reload and runtime change derive polarity through the same helper.

### Resource bounds

- Descriptor graphs at each exact capacity pass.
- One excess endpoint, input cluster, output cluster or attribute fails before a
  destination sentinel changes.
- Duplicate endpoint ids, missing pointers and unregistered clusters fail with
  the expected reason.
- Fuzzed small descriptor graphs run under ASan/UBSan.

### `0xFC02` queue

| Case | Expectation |
| --- | --- |
| enqueue burst | one submission now, remaining submissions paced |
| send reject then accept | front retained, same sequence retried, `send_failed == 1` |
| queue full | oldest evicted, newest retained, `dropped == 1` |
| stale front | one expiry per callback, newer entries progress |
| network loss | pump pauses; no busy retry |
| rejoin inside TTL | queue resumes in FIFO order |
| rejoin after TTL | entries expire and no stale command is submitted |
| occupancy rise/fall | high-water records maximum, not current value |
| millis wrap | pacing and TTL remain correct |

## Phase 1 stub tests

Extend the stub with:

- explicit ISR-context guards around fake task APIs;
- coordinator-send rejection for next `N` attempts or until virtual time `T`;
- descriptor validation command/output;
- network transition and announce failure injection;
- complete diagnostic snapshots.

Required pytest coverage:

| Area | Test intent |
| --- | --- |
| rapid button events | received `0xFC02` commands preserve order and spacing |
| event failure injection | retries, TTL and distinct counters |
| invalid Zigbee write | invalid endpoint/cluster never crashes or writes NVM |
| polarity | all token/pull combinations act on the correct level |
| target config | exact BSEED config constructs 8 endpoints and passes preflight |
| network monitor | callback plus poll duplicates one transition only |
| announce | failure does not set success; bounded retry succeeds |

## Phase 2 native and stub tests

### Startup settle

- Edges before 250 ms update suppression diagnostics but produce no action.
- A sampled-down input stays suppressed through release.
- First full post-settle press produces one action.
- Each relay startup mode wins over quarantined input.

### Rejoin resync

- Already-joined initialization produces no recovery plan.
- `NOT_JOINED/JOINING → JOINED` produces one plan.
- One report is attempted per 75 ms step in deterministic endpoint order.
- A state update replaces a pending state snapshot.
- Report/announce failure retries with backoff; no action event is synthesized.

### Overflow reconciliation

- Dropped release clears a stuck button and cancels gesture timers.
- Dropped press does not toggle or bind.
- Multiple-button resample converges all state.
- A new overflow during repair schedules another epoch.

### OTA validation

Use tiny fixture YAML files for unique, duplicate, expanded-known-duplicate,
invalid type and null cases. Separately assert that the real database contains
the BSEED key with image type `43627` exactly once without dumping the file.

### NVM

- Coalescing, retry, multiple owners, reset discard and reboot flush use virtual
  time and an injectable fake HAL.
- Persisted bytes are compared with the final owner state.
- Failure counters and dirty counts are observable through stub diagnostics.

## Telink static and build gates

Run:

```bash
make stub/build
make tests
make format
BOARD=SWITCH_BSEED_TS0726_4GANG DEVICE_TYPE=router make board/build
```

Additionally:

- inspect the call graph rooted at `gpio_isr_callback`; no task/timer post or
  cancel, Zigbee, NVM or relay symbol is reachable;
- search for direct unchecked endpoint table indexes and cluster-count appends;
- record endpoint/input-cluster/output-cluster/attribute preflight totals from
  the target build;
- treat new compiler warnings as failures.

## BSEED hardware matrix

Use the exact router image and record firmware commit, coordinator hardware,
coordinator firmware, Zigbee2MQTT/ZHA version, channel, mains/load and device
distance.

### Input and transport

| Test | Count | Pass criterion |
| --- | --- | --- |
| normal tap per gang | 100 each | relay transitions and received press cycles equal physical cycles |
| fastest manual tap per gang | 100 each | no issue-438 loss; all loss counters zero |
| two-gang overlap | 100 pairs | both relays and both event streams present |
| four-gang overlap | 50 sets | four relay changes and four ordered streams per set |
| 2/3/5/10 click bursts | 10 each/gang | received count and sequence continuity exact |
| forced TX rejection | 1 second | retries recover; gaps explained only by expiry/drop counters |

### Power cycle

For every startup mode, run 50 released-button breaker cycles and 10 cycles with
one button held. Verify relay contacts, Zigbee states, binding capture and startup
diagnostics.

### Network recovery

- baseline coordinator outage of 1, 5 and 30 minutes;
- verify recovery with no physical input;
- verify paced state convergence for all eight endpoints;
- verify announce retry and no event replay;
- repeat while generating local button traffic before and after outage.

## Soak gates

### Phase 1

Minimum seven days on BSEED hardware: 72 hours uninterrupted plus controlled
outages and daily input bursts. Pass criteria are defined in
[06_router_liveness.md](./06_router_liveness.md).

### Phase 2

Minimum seven additional days on the complete phase-2 image with at least 100
breaker cycles, three long coordinator outages and configuration-write bursts.
End-of-run counters must show:

- zero unexplained GPIO/event loss;
- zero descriptor failures;
- zero NVM write failures;
- expected startup suppression only during power cycles;
- expected network transition and resync counts;
- no unexplained reset.

## Evidence retention

Update [STATE.md](./STATE.md) with the commit and summary. Store large UART,
packet-capture and coordinator logs outside `docs/`; link or identify them in the
hardware evidence row rather than committing generated traces.

