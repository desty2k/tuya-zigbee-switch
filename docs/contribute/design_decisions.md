# Design decisions

Choices that shape the firmware and are easy to undo by accident. Each entry
gives the decision, why it was made, and what follows from it in the code.
How the parts fit together is in [architecture.md](./architecture.md); work
not done yet is in [open_work.md](./open_work.md).

## Input

**The edge queue belongs to the application, not the HAL.** — The HAL only
timestamps level changes and hands them to one interrupt-safe edge sink; it
knows nothing about buttons. This keeps buffering policy out of three HAL
implementations and makes the queue testable without platform code. —
`gpio_edge_queue` is owned by `button_input`; `hal_gpio_set_edge_sink()` takes
a single sink.

**Interrupt handlers capture and nothing else.** — The Telink SDK timer and
task functions are not known to be interrupt-safe, so no generic scheduling
API is assumed safe in an interrupt. — The GPIO interrupt reads levels,
timestamps, pushes to the queue and sets pending flags; `app_task()` calls
`hal_gpio_process_pending()` and `button_input_process_pending()`, which
schedule the work.

**Debounce works on edge timestamps, not on when the worker runs.** — A level
is committed when a later edge or the clock proves it lasted the debounce
time, so a short valid press survives a late worker. An edge carrying
`HAL_GPIO_EDGE_DEFERRED` (seen late, from the main loop) proves nothing about
how long the previous level lasted, so a glitch seen during a main-loop stall
does not become a press. — `button_input_process_edge()`; the worker samples
`hal_millis()` only after draining the queue.

**Debounce default 25 ms.** — Mains pickup on long switch wiring has 8.3–10 ms
half-periods; anything at or below that commits ghost presses. 25 ms keeps a
2.5× margin over a 50 Hz half-cycle with less latency than 50 ms. —
`BUTTON_DEBOUNCE_DEFAULT_MS`, `CONFIG_DEFAULT_DEBOUNCE_MS`; boards and users
override it with `D<ms>` or the per-endpoint attribute.

**Multi-click gap default 350 ms.** — Long enough for a ten-click sequence on a
wall switch, short enough that a single click, which is reported only after
the gap, is not noticeably delayed. — `G<ms>` and the per-endpoint attribute change it.

**The tenth click is reported at once; a hold flushes pending clicks.** — The
count cannot grow past `GESTURE_MAX_N_CLICK` (10), so waiting for the gap only
adds delay (the ten-click factory reset fires on the tenth release). Clicks
before a hold are emitted before `HOLD_START` so mixed gestures stay
observable. — `gesture_fsm.c`.

**After lost edges, the inputs are re-read.** — Once an edge is dropped the
order of events is unknowable; the only safe state is the pin's current level.
— A failed push sets a resync flag; after draining, the worker feeds every
input's current level as a deferred edge, so a dropped release cannot leave a
button down.

**Polarity comes from the pull.** — A pull-down input is pressed high, a
pull-up input pressed low, for `B`, `S` and both `X` inputs alike. The switch
mode (momentary NC) inverts relative to that electrical baseline instead of
overwriting it. — `feature_wiring.c` stores `electrical_active_high`; a mode
change re-seeds the button from the pin without emitting events.

## Relays and covers

**One owner per relay.** — Interlock, inching and timed-off can only be
guaranteed when every change passes one place. — `relay_ctrl_submit()` is the
only entry; only `relay_driver` writes relay pins; timer expiry re-enters the
controller with `RELAY_SOURCE_TIMER`.

**Latching coil pulses are separate from inching.** — The 100 ms coil pulse is
a hardware detail of bistable relays; inching is a user feature. Sharing a
timer or a request type would break every latching device. — Coil pulses live
in `relay_driver`, `RELAY_REQUEST_PULSE` in `relay_controller`.

**Inching applies to every ON.** — An impulse load (garage door opener) must
not stay energised because one source forgot to ask for a pulse. — Any `ON`,
from Zigbee, bindings, buttons or power-on behaviour, becomes a pulse while
`InchingDuration` is non-zero; `ON_TIMED` is capped at the inching duration.

**The first request after boot always reaches the driver.** — A latching
relay's contact state is unknown at boot even though the logical state starts
off. — `state_applied` in `relay_controller.c`; later requests for the
current state return `RELAY_RESULT_UNCHANGED`.

**Controllers return domain results.** — Controllers must not depend on Zigbee.
— `relay_result_t` and the cover results are mapped to ZCL statuses in
`relay_cluster.c` and `cover_cluster.c`; no base-component header includes
`hal/zigbee.h`.

**The config string defines interlock groups.** — A saved attribute must never
take a relay out of a group the hardware config requires (a motor's two
directions). — `K` groups are numbered 1..n in config order, cover groups
follow; `InterlockGroup` writes accept only the relay's own `K` group, or for
an ungrouped relay a `K` group or 0; relay records are deleted when the relay
topology changes (`NV_ITEM_RELAY_TOPOLOGY`).

**Interlock dead time defaults to 200 ms and starts at an actual OFF.** —
200 ms lets a de-energised contact open and a motor's back-EMF decay. A group
whose members are already off has nothing to wait for. — `K<mask>` without
`:ms` uses `DEVICE_COMPOSITION_DEFAULT_DEAD_TIME_MS`; a deferred pulse or
timed-on starts its deadline when the target is energised.

**Interlock is the only reversal guard of a cover.** — Two timers enforcing the
same gap drift apart. — The cover's relay pair is an interlock group with the
default dead time; `cover_controller` starts motion timing when the relay
observer reports the relay actually on.

**Cover position is basis points, 0 closed to 10000 open.** — Integer
arithmetic on Telink: travel times up to 429496 ms keep every product in
32 bits. — The Window Covering adapter reports
`100 - round(position / 100)` (ZCL: 0 = open) and `0xFF` while the position is
unknown or the cover has no travel times.

**An unknown position is never upgraded by a guess.** — Timing a move from a
stale stored position runs the cover short and then claims a known end. —
While unknown, open and close run full travel plus overrun and only a
completed end move makes the position known; going to an intermediate
percentage is refused; stop never raises confidence. A `motion_active` marker
is stored before the motor starts, so a power loss mid-move boots unknown.

## Zigbee interface

**Events are commands, state is attributes.** — A click is something that
happened; an attribute that is overwritten loses rapid repeats. — Button
actions travel as `0xFC02` commands with a per-device sequence number;
Multistate Input `PresentValue` stays a pure state attribute.

**Button events go to the coordinator, not to bindings.** — They are
notifications; control commands (On/Off, Level) already go to bindings. —
`hal_zigbee_send_cmd_to_coordinator()`.

**The button event queue is paced.** — A burst handed to the stack in one
loop can be dropped by the stack. — One submission per 40 ms, retry after 100 ms,
5 s time to live, distinct counters for evicted, expired and failed events.
"Submitted" means accepted by the stack, not delivered.

**One manufacturer code for every fork-private cluster.** — The integrations
must send the code the firmware checks; one code generated from one file
cannot diverge between them. — `0x1A2B`
(`helper_scripts/management_protocol.json`) on `0xFC01`, `0xFC02`, `0xFC10`,
`0xFC11`, `0xFC12`, generated into firmware, tests and templates.

**Management lives on fork-private clusters; Basic is identity only.** — A
fixed management interface works in every configuration and in recovery. —
Device Config `0xFC10` and Device Diagnostics `0xFC11` on endpoint 1; Basic
carries only standard attributes.

**COMMIT owns the attribute storage.** — On Telink the write hook runs before
the SDK copies the value, so a callback reading its storage would apply the
old value. — The two-phase write contract in `src/hal/zigbee.h`: COMMIT receives
the candidate and stores it; the HAL never copies over the storage of a
cluster with a callback.

**Configuration fails closed.** — A partly registered graph can leave a device
with no endpoint to repair it from. — The graph is counted against the
smallest platform limits (`zigbee_limits.h`) before registration; any failure
boots the recovery graph, and a config that would not fit is refused when it
is written.

## Build and platforms

**Features are compiled per board.** — Flash on the TLSR8258 is too small for
every feature in every image. — `features` in `device_db.yaml` (or derived
from the config tokens) selects sources in `src/features.mk`; a token for a
feature the image lacks boots the recovery graph with `FEATURE_MISSING`.

**Own code at `-Os`, SDK at `-O2`, no jump tables.** — The image must fit the
200704-byte bootloader-layout OTA slot. tc32-gcc 4.5.1 at `-Os` emits byte
switch tables that it dispatches as 32-bit addresses, which resets the
device. — `src/telink/Makefile`: `-Os` for `common/` and `telink/` objects,
`-fno-jump-tables` for all, and `helper_scripts/check_tc32_jump_tables.py`
fails the link on any table dispatch or untraced indirect jump.

**`hal_millis()` wraps at 2^32 on every platform.** — Telink's 16 MHz tick
wraps every 268 s; fixing each `now - then` consumer would miss one. —
`hal/tick_clock.h` extends the tick; the main loop and a refresh timer before
long sleeps keep it updated.

**Meter pulses use a counter HAL, not the edge queue.** — Kilohertz pulse
trains would flood the button path, and read-and-reset counting loses pulses.
— `src/hal/gpio_counter.h` with non-destructive snapshots; on Telink counter n
is timer n on GPIO-to-RISC source n, which the GPIO probe also uses, so
`meter` and `probe` never share an image. Energy is owned by `meter_service`,
not by the driver or a cluster.

**Active power is reported in W.** — 0.1 W would end at 3276.7 W, below the
3680 W of a 16 A outlet. — `src/zigbee/meter_units.h`.

**Unclaimed pins rest on the pull-down in the probe image and the recovery
graph.** — A
relay driver input behind a high-value base resistor picks up mains hum when
floating and chatters the relay. — `hal_gpio_rest_all()`.
