# 13 — Implementation and release sequence

Implementation is split into two phases. Each stage replaces the unsafe path in
the same change; no disabled alternative implementation or compatibility adapter
remains.

```text
Phase 1 — stability gate
  1 ISR/task isolation
  2 safe Zigbee dispatch + pull-derived polarity
  3 endpoint/cluster/attribute bounds
  4 paced 0xFC02 transport
  5 router liveness diagnostics
  6 BSEED phase-1 regression and soak

Phase 2 — operational hardening
  7 startup input settle
  8 rejoin state resynchronization
  9 GPIO overflow reconciliation
 10 OTA image-type validation
 11 NVM write coalescing + failures
 12 BSEED phase-2 regression and soak
```

## Phase 1 — stability gate

### Stage 1 — ISR/task isolation

Scope: add normal-context GPIO and button pending processors; remove every
`hal_tasks_schedule/unschedule` path reachable from Telink GPIO IRQ; preserve the
SPSC edge ring and capture timestamps.

Gate: native ISR guard and race tests; static call audit; `make stub/build` and
full tests.

### Stage 2 — Zigbee dispatch and polarity

Scope: checked endpoint lookup helpers and NULL-safe write/command trampolines;
one pull-derived polarity helper for `B`, `S` and `X`; mode changes derive and
reseed through one path.

Gate: invalid endpoint matrix; pull/mode matrix; exact BSEED config released at
boot; full tests.

### Stage 3 — resource bounds

Scope: shared checked descriptor builder; endpoint-id capacity constants; Telink
aggregate preflight and all-or-nothing registration; matching Silabs buffer
preflight only where shared contract requires it.

Gate: exact and capacity+1 tests under sanitizers; BSEED eight-endpoint graph
accepted; no unchecked append/index patterns; Telink board build.

### Stage 4 — paced event transport

Scope: replace the drain loop with a one-attempt TX pump; propagate immediate
platform send status; retry, expiry and high-water counters; stub failure
injection; Basic-cluster diagnostics and generator-template updates.

Gate: native/stub failure matrix; target build; BSEED fast-press and overlap
matrix with coordinator packet/application capture.

### Stage 5 — liveness instrumentation

Scope: deduplicating network monitor; reset/uptime/network/steering/rejoin and
announce diagnostics; boot announce retry; compact UART events. Do not alter
Telink rejoin timing without evidence.

Gate: stub transition/retry tests; diagnostics readable from BSEED; controlled
coordinator outages explained by logs and counters.

### Stage 6 — phase-1 hardware gate

Scope: no new code. Run the complete phase-1 matrix and seven-day soak from
[12_testing.md](./12_testing.md).

Gate: all phase-1 criteria pass on one unchanged commit. The branch cannot be
called stable while this stage is blocked or while issue-438/255 behavior is
unexplained.

## Phase 2 — operational hardening

### Stage 7 — startup input settle

Scope: 250 ms input quarantine, settled resample, boot-held suppression and
startup counters. Relay startup policy stays in the relay controller.

Gate: virtual-time tests plus BSEED breaker-cycle matrix focused on issue 460.

### Stage 8 — rejoin resynchronization

Scope: genuine recovery detection; immutable/staged state plan; paced relay and
button state reports; shared announce retry; no action replay.

Gate: stub transition/failure tests and 1/5/30-minute hardware outages with
state convergence and no physical interaction.

### Stage 9 — overflow reconciliation

Scope: overflow generation marker, post-drain GPIO resample, state repair and
gesture cancellation without relay/binding action.

Gate: forced dropped-UP and dropped-DOWN native/stub tests; hardware counters
remain zero in ordinary stress runs.

### Stage 10 — OTA image validation

Scope: comparison-mode CI checker, full audit target, board-build check and unit
fixtures. Preserve image type `43627`; do not regenerate migration artifacts.

Gate: validator tests and unchanged BSEED OTA metadata/artifacts.

### Stage 11 — NVM coalescing

Scope: fixed-slot delayed commit service; migrate cluster configuration owners;
retry/failure diagnostics; reset discard and reboot flush rules.

Gate: virtual-time/failure tests, final-byte verification, input/TX latency
regression and target build.

### Stage 12 — phase-2 hardware gate

Scope: complete phase-2 regression and seven additional soak days on one commit.

Gate: every criterion in [12_testing.md](./12_testing.md), including 100 breaker
cycles, long coordinator outages, NVM bursts and clean end-of-run counters.

## External compatibility

- Existing endpoint ids, cluster ids, attribute ids/types and button-event
  payload remain unchanged.
- `LastEventSeq` continues to mean accepted stack submission, now paced.
- No queued action is persisted across reboot.
- Relay startup modes preserve their Zigbee semantics.
- BSEED firmware image type remains `43627` and OTA from released firmware stays
  possible.
- New Basic diagnostics are additive and mirrored in generated integrations.

## Required verification for every code stage

1. `make stub/build` succeeds without new warnings.
2. `make tests` passes with targeted tests added.
3. `make format` leaves no further source diff.
4. The BSEED Telink router build succeeds.
5. Any platform path not built locally is called out in the stage evidence.

Generated Zigbee2MQTT, ZHA, HOMEd, OTA and supported-device outputs are updated
only when their source templates or data changed, and are committed separately
from source changes.

