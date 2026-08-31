# 11 — NVM write coalescing and error counters

Phase 2, priority 5.

## Problem

Attribute-write callbacks and relay state changes can synchronously call
`hal_nvm_write()`. Rapid writes therefore add flash latency to Zigbee/input paths
and repeatedly persist intermediate states. Return statuses are commonly
ignored, so failure is invisible.

## Service contract

```c
typedef bool (*nvm_commit_snapshot_fn)(void *owner);

void nvm_commit_mark_dirty(uint8_t item_id,
                           nvm_commit_snapshot_fn commit,
                           void *owner);
bool nvm_commit_flush(uint8_t item_id);
bool nvm_commit_flush_all(void);
void nvm_commit_discard_all(void);
```

No dynamic allocation is used. A fixed table is sized for the application-owned
items that can be dirty concurrently. Each item id has exactly one owner.

## Dirty and commit semantics

- First mutation marks the item dirty and schedules commit for
  `now + NVM_COMMIT_DELAY_MS` (initially 1000 ms).
- Later mutations before the deadline update owner RAM state but do not move the
  deadline indefinitely.
- At the deadline, the owner copies a complete persistence struct into its
  existing module-local snapshot buffer and performs one write.
- Success clears dirty and increments `nvm_commits`.
- Failure retains dirty, increments `nvm_write_failures` and retries with bounded
  backoff.
- A callback commits at most one item; multiple dirty items are serialized.

The service never stores pointers to stack data. Persisted struct layout and NVM
item meaning remain unchanged, so coalescing alone does not require a migration.

## Initial owners

Apply coalescing to high-frequency paths first:

- switch cluster configuration;
- relay cluster configuration, including persisted previous state;
- cover and cover-switch configuration;
- Basic cluster manual indicator configuration;
- poll-control configuration where applicable.

Device-config replacement, migration version writes and one-time device-type
writes remain explicit synchronous operations because they participate in a
reboot or migration transaction.

## Reset and reboot behavior

| Action | Dirty-item behavior |
| --- | --- |
| factory reset | discard dirty application items, then clear NVM; never rewrite them |
| reboot after device-config write | synchronously verify the device-config write, flush other required items, then reboot |
| ordinary scheduled reboot | flush all; if a required write fails, expose failure and delay reboot within a bounded policy |
| watchdog/power loss | latest uncommitted changes may be lost; persisted data remains a complete older snapshot |

Coalescing guarantees atomic snapshots at item granularity, not durability before
the commit deadline.

## Shared buffers

Several cluster modules use one static `nv_config_buffer` across instances.
Delayed commits must fill that buffer immediately before each write; they must
not retain a pointer to buffer contents prepared for another endpoint.

## Diagnostics

| Counter/value | Meaning |
| --- | --- |
| `nvm_dirty_items` | current number of dirty slots |
| `nvm_commits` | successful delayed writes |
| `nvm_updates_coalesced` | dirty marks absorbed before commit |
| `nvm_write_failures` | failed HAL writes |
| `nvm_last_failed_item` | last item id whose write failed |

## Acceptance

- One hundred updates to one attribute inside 1 second produce one commit with
  the final value.
- Updates to four relay items produce four serialized writes, not one shared
  buffer corruption.
- Injected NVM failure retains dirty state and retries; the counter increments.
- Millisecond wraparound does not postpone a commit indefinitely.
- Factory reset performs no delayed rewrite afterward.
- Reboot tests prove required config is durable before reset.
- Input latency and `0xFC02` pacing tests remain unchanged while commits occur.

