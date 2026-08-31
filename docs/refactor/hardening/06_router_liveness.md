# 06 — TLSR8258 router liveness diagnostics and soak

Phase 1, priority 5. This is an investigation gate for upstream issue 255, not a
speculative networking-policy rewrite.

## Observed failure class

Issue 255 describes router firmware becoming unavailable for roughly one to five
minutes, recovering either spontaneously or immediately after physical input.
Possible causes include radio/network state, a stalled application loop,
coordinator availability heuristics, reset/reboot, reporting backlog or power
integrity. The symptom alone does not identify one.

## Network monitor

A normal-context monitor samples `hal_zigbee_get_network_status()` once per
application poll and records transitions. Platform callbacks still provide fast
notification, but the sampled monitor is authoritative so a Telink SDK state
change that lacks an application callback is visible.

```c
typedef struct {
    hal_zigbee_network_status_t current;
    uint32_t transition_count;
    uint32_t network_loss_count;
    uint32_t joined_count;
    uint32_t last_transition_ms;
    uint32_t last_joined_ms;
} network_diagnostics_t;
```

Callbacks and polling feed one deduplicating transition function. Repeated
samples of the same state do not increment counters or restart recovery work.

## Required evidence

| Signal | Purpose |
| --- | --- |
| uptime | distinguish a reboot from a temporary network loss |
| reset cause | identify watchdog, power-on/brownout, software and external reset where Telink exposes them |
| network status and last transition time | correlate coordinator unavailability with firmware state |
| loss/join transition counts | identify repeated rejoin cycles |
| steering attempts and immediate failures | detect retry storms or rejected starts |
| rejoin requests | correlate SDK recovery with outage windows |
| announce attempts/failures | prove whether presence notification was accepted |
| watchdog-loop heartbeat | prove the main loop continues to run during a reported outage |
| `0xFC02` queue counters | separate event transport pressure from network loss |
| GPIO/button counters | determine whether physical activity merely creates traffic or wakes stalled work |

The debug UART prints one compact line per network/reset event, never one line per
superloop. Basic-cluster attributes expose counters needed without a probe.

## Steering discipline

The existing Telink protections remain: only start steering when neither joined
nor joining, trust the immediate start result, and clear the in-progress flag on
all completion/failure paths. Instrument attempt/result first. Any backoff or
rejoin policy change requires evidence of an actual storm or stranded state in
the phase-1 soak.

## Announce correctness

`boot_announce_sent` becomes an announce state machine. It is marked complete
only after `hal_zigbee_send_announce()` returns `HAL_ZIGBEE_OK`. Immediate
failure increments `announce_failures` and schedules a bounded retry rather than
retrying every superloop pass. Phase 2 generalizes the same mechanism for rejoin.

## Soak protocol

Run the phase-1 build on the installed or electrically representative BSEED
4-gang for at least seven continuous days:

- coordinator and normal mesh remain powered for a 72-hour baseline;
- coordinator is then powered off for 1, 5 and 30 minutes and restored;
- daily fast/overlapping button bursts exercise local and `0xFC02` paths;
- Zigbee2MQTT availability, coordinator traffic, diagnostic snapshots and UART
  events are timestamp-correlated;
- record mains/load configuration and firmware commit.

## Pass criteria

- No unexplained unavailable interval longer than the coordinator's configured
  availability timeout.
- No reset except deliberate test resets.
- No main-loop heartbeat stall.
- Every forced coordinator outage produces an explainable status transition and
  recovery.
- Physical activity is not required for recovery.
- No steering/rejoin attempt storm and no announce failure left without retry.
- GPIO, descriptor and `0xFC02` loss counters remain zero during clean-network
  portions.

If the failure reproduces, the evidence determines a separate minimal fix. This
stage is not complete merely because the issue fails to reproduce in a short
bench run.

Reference: [upstream issue 255](https://github.com/romasku/tuya-zigbee-switch/issues/255).

