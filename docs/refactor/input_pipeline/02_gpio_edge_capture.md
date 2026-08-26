# 02 — GPIO edge capture and edge queue

## HAL contract

```c
typedef struct {
    hal_gpio_pin_t pin;
    uint8_t        level;        // 0 or 1, level after the transition
    uint32_t       timestamp_ms; // hal_millis() at capture
    uint32_t       seq;          // monotonic, incremented per emitted edge
} hal_gpio_edge_t;

/** Called for every captured transition. May run in interrupt context. */
typedef void (*hal_gpio_edge_sink_t)(const hal_gpio_edge_t *edge);

void hal_gpio_set_edge_sink(hal_gpio_edge_sink_t sink);
void hal_gpio_watch_pin(hal_gpio_pin_t pin);
void hal_gpio_unwatch_pin(hal_gpio_pin_t pin);
```

`hal_gpio_callback()` / `hal_gpio_unreg_callback()` are removed. `hal_gpio_init`,
`hal_gpio_read`, `hal_gpio_set`, `hal_gpio_clear`, `hal_gpio_parse_pin`,
`hal_gpio_parse_pull` keep their current signatures.

Semantics of an edge: *"pin P reached level L at time T"*. Nothing else. The HAL
knows nothing about buttons, debounce, presses or relays.

## Requirements

1. **No coalescing.** `DOWN → UP → DOWN` must produce three edges even when the
   final level equals the initial level.
2. **Level tracking.** The HAL keeps `last_level[pin]` for every watched pin and
   emits an edge only when the observed level differs from `last_level`.
   `last_level` is initialised from a read at `hal_gpio_watch_pin()` time, so no
   synthetic edge is produced for the boot state.
3. **Masked interrupts are allowed, lost transitions are not.** When a platform
   must temporarily disable or re-arm interrupts, it uses the capture procedure
   below.
4. **Timestamps come from capture time**, not from consumption time.
5. **Sink is IRQ-safe.** Implementations may call the sink from an ISR. The sink
   only pushes to the edge ring and schedules the worker task.

### Capture procedure for single-edge-polarity platforms

```text
capture():
    now = hal_millis()
    snapshot = read all watched pins
    for each watched pin with snapshot[pin] != last_level[pin]:
        last_level[pin] = snapshot[pin]
        emit edge {pin, snapshot[pin], now, seq++}

on IRQ:
    capture()
    disable watched IRQs
    schedule dispatch task (0..3 ms)

dispatch task:
    tries = 0
    do:
        prev = snapshot of watched pins
        set IRQ polarity per current level for every watched pin
        capture()                       # emits edges seen while re-arming
        tries++
    while snapshot changed and tries < MAX_REARM_TRIES
    enable watched IRQs
    capture()                           # catches changes during enable
```

`MAX_REARM_TRIES = 50`. Reaching the limit increments
`gpio_rearm_limit_hits`.

## Per-platform implementation

| Platform | Interrupt model | Notes |
| --- | --- | --- |
| Telink TLSR825x | One shared IRQ, single configurable polarity per pin | Uses the full capture procedure above; `drv_gpio_read_all` provides the snapshot; ISR-side `capture()` timestamps the first transition of a burst accurately |
| Silabs EFR32 | Per-pin EXTI, rising+falling | ISR reads the pin, timestamps and calls `capture()` for that pin only; no re-arm loop needed |
| Stub (host) | Test injection | `stub_gpio_simulate_input()` and `stub_gpio_inject_edges()` call `capture()` with the current virtual clock |

Deep-retention wake (Telink end devices): after wake, GPIO SFRs are replayed and
`capture()` runs once. Level changes that happened while sleeping surface as a
single edge timestamped at wake time. Transitions that both began and ended
during sleep are not recoverable and are not simulated.

## Edge queue

```c
#define GPIO_EDGE_QUEUE_SIZE 32   // power of two

typedef struct {
    hal_gpio_edge_t   items[GPIO_EDGE_QUEUE_SIZE];
    volatile uint8_t  head;       // producer only
    volatile uint8_t  tail;       // consumer only
    volatile uint32_t dropped;
} gpio_edge_queue_t;

void gpio_edge_queue_init(gpio_edge_queue_t *q);
bool gpio_edge_queue_push(gpio_edge_queue_t *q, const hal_gpio_edge_t *edge);
bool gpio_edge_queue_pop(gpio_edge_queue_t *q, hal_gpio_edge_t *out);
uint8_t gpio_edge_queue_used(const gpio_edge_queue_t *q);
```

Rules:

- Single producer (ISR or HAL task), single consumer (worker task).
- Entries are immutable once pushed. A newer edge never overwrites a pending one.
- On overflow the push fails, the edge is discarded and `dropped` is incremented.
  The queue is not resized and older entries are not evicted.
- Order is preserved regardless of consumer latency: the sequence
  `DOWN@10, UP@30, DOWN@50, UP@70` is delivered intact to a consumer that first
  runs at `t=100`.

## Diagnostics

Counters live in `gpio_edge_queue` / `hal_gpio` and are exposed through the Basic
cluster (see [08_zigbee_events.md](./08_zigbee_events.md)) and the stub REPL
`diag` command.

| Counter | Meaning |
| --- | --- |
| `gpio_irq_count` | Number of GPIO interrupt entries |
| `gpio_edges_captured` | Edges emitted by the HAL |
| `gpio_edges_dropped` | Edges lost due to queue overflow |
| `gpio_rearm_limit_hits` | Capture loop hit `MAX_REARM_TRIES` |
| `button_events_emitted` | Logical DOWN/UP events committed |
| `gestures_emitted` | HOLD_START + HOLD_END + N_CLICK events |
| `zb_button_events_dropped` | Button events dropped before transmission |
