# Decisions

## Accepted

1. **The BSEED TLSR8258 router is the release target.** Cross-platform contracts
   remain valid, but Silabs-only recovery work and battery/end-device tuning do
   not gate this branch.

2. **No periodic upstream merge policy.** Small correctness fixes are ported by
   concept and covered by local tests. Unrelated endpoints, clusters and features
   are not imported.

3. **No task API is IRQ-safe by assumption.** GPIO interrupt code may timestamp,
   snapshot, push to an SPSC ring, update IRQ-owned counters, disable/re-arm GPIO
   hardware and set `volatile` work flags. Posting or cancelling generic tasks is
   normal-context work.

4. **Pacing is the phase-1 `0xFC02` reliability mechanism.** Only one queue entry
   is submitted per pacing interval. An immediate stack rejection retains the
   entry for retry. A successful submission is not described as RF delivery
   unless the HAL later exposes a real completion indication.

5. **Transport losses have distinct counters.** Queue eviction, expiry and
   immediate send failure are not merged into one number. Queue high-water is
   retained until reboot so a healthy run still reveals proximity to capacity.

6. **Invalid Zigbee dispatch is ignored safely.** Endpoint lookup uses a checked
   helper. Attribute-write and command trampolines never index directly by an
   untrusted endpoint and never call a cluster function with `NULL`.

7. **Electrical pull determines default active level.** Pull-down means pressed
   high; pull-up means pressed low for `B`, `S` and both `X` inputs. Runtime switch
   mode may change semantics only through an explicit, validated configuration
   path.

8. **Configuration construction fails closed.** Endpoint, cluster and attribute
   counts are validated before the Zigbee stack receives a descriptor. No
   platform silently truncates or writes beyond a fixed buffer.

9. **The exact-capacity BSEED layout remains supported.** Telink supports eight
   endpoints numbered 1 through 8. Dispatch tables use endpoint-number capacity,
   including a non-addressable slot zero, rather than relying on `[10]` folklore.

10. **Issue 255 starts with evidence, not a speculative patch.** Network-state
    transitions, steering/rejoin attempts, announce results, uptime and reset
    cause are observable before changing Telink rejoin policy.

11. **Startup input is quarantined, not replayed.** Edges captured during the
    settle interval produce no relay, binding, gesture or `0xFC02` action. The
    final sampled level seeds the normal input FSM.

12. **Rejoin resync is transition-based.** State snapshots and an announce are
    scheduled only after an observed non-joined state becomes joined. Callback
    registration while already joined is not a rejoin.

13. **Overflow recovery converges to hardware truth.** Once an edge is dropped,
    ordering is unknowable. After preserved entries drain, every watched pin is
    resampled and affected button FSMs are reseeded without inventing gestures.

14. **OTA uniqueness validation lands without renumbering the fleet.** CI rejects
    newly introduced duplicate `firmware_image_type` values. The BSEED id `43627`
    remains unchanged. Any later renumbering requires a separately reviewed OTA
    migration plan.

15. **NVM coalescing is item-oriented and reset-aware.** A dirty item has one
    owner, one RAM snapshot and one delayed commit. Factory reset discards dirty
    application items; an intentional reboot flushes required items before reset
    and exposes failure.

## Deliberately deferred

- APS-confirm-driven `0xFC02` delivery if the Telink HAL cannot expose it without
  bypassing the normal ZCL path.
- Broad image-type consolidation or the upstream set of image-id migrations.
- Silabs steering, orphan recovery and watchdog changes from upstream PR 477.
- Multistate action redesign, companion long-press endpoints and battery router
  parity work.

