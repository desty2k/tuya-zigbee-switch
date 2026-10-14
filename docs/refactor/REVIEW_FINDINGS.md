# Review findings and fix plan — `refactor/input-pipeline`

Review date: 2026-10-14. Reviewed snapshot: commit `d71a3bb5` (source, tests,
docs) and `2c2d2bd7` (development OTA images and index entries), on top of
`538f75d3`.

This document is the work order for the agent that implements the fixes. It
lists every finding from the review, where it is, why it is wrong, what breaks
on a physical device, and how to fix and test it. Work through the steps in
order.

## 1. Summary

The branch is **not safe to flash or OTA onto real devices**. The core design
(GPIO edge capture → button input → gesture FSM → action mapping → relay
controller with interlock) is sound. The defects sit where the stub does not
model the real platforms: Telink/Silabs reporting, Telink write ordering, SDK
table limits, interrupt timing and clock wrap. Because the stub is more
permissive than the hardware, `make tests` stays green while real devices
would misbehave.

Branch state at review time:

- Three refactor plans are stacked in `docs/refactor/` (`input_pipeline`,
  `hardening`, `capabilities`). No hardware gate of any plan has been run;
  every hardware stage is `blocked` or `deferred`.
- Capability phases 3–6 were uncommitted until `d71a3bb5`.
- Silabs code has never been compiled on this branch: the SLC generator did
  not produce `zigbee.Makefile`, see `capabilities/STATE.md` evidence log.
- `make tests`: 279 passed, 1 failed on macOS (see S1). The `STATE.md` "all
  green" entries are stale.
- `make stub/build` has a new clang warning at `src/stub/commands.c:332`
  (`attr->size > sizeof(candidate)` is always false, since `size` is `uint8_t`).

Verification status labels used below:

- **verified**: confirmed by reading the code during the review, and for C3
  also by running the stub.
- **reported**: found by a review agent reading the code, plausible, not
  re-checked line by line. Confirm before fixing.
- **unverified**: depends on SDK or hardware behaviour nobody has checked yet.

## 2. Rules for the implementing agent

1. Read `AGENTS.md`, including the context hygiene table, before starting.
2. Work in this order:
   1. Step 0: housekeeping.
   2. Step 1: make the stub model the real platforms (S-items).
   3. Step 2: add or adjust tests so they **fail** on the current behaviour.
   4. Step 3: fix firmware behaviour until the tests pass (C, H, M, L items, by
      severity).
3. Commit each logical fix separately, with its tests. Regenerated artifacts go
   in their own commit, as `AGENTS.md` requires.
4. Each step must pass `make stub/build` (no new warnings), `make tests`, and
   `make format` with no further diff.
5. Telink changes: if `make telink/build` or
   `BOARD=SWITCH_BSEED_TS0726_4GANG make board/build` cannot run locally, say
   so in the summary. Silabs changes are **unverified** until a Silabs build
   actually compiles; state that in every summary.
6. Do not start capability phases 5–9 (see D4).
7. Items marked **decision** need the owner's answer first. If no answer is
   available, implement the stated default.
8. Update `docs/refactor/*/STATE.md` with evidence that can be reproduced:
   command, commit and result. Do not record results that were not rerun.

## 3. Step 0 — housekeeping

### H0.1 Remove development builds from the release OTA index — critical

- **Where:** `zigbee2mqtt/ota/index_router.json` (4 entries) and
  `zigbee2mqtt/ota/index_router-FORCE.json` (2 entries); `bin/router/SWITCH_BSEED_TS0726_{3,4}GANG/`.
  All were committed in `2c2d2bd7`.
- **Problem:**
  - The entries point at
    `github.com/desty2k/.../raw/refs/heads/refactor/input-pipeline/...`, a
    mutable branch on a fork. `main` has no fork URLs.
  - Their file version (285356061) is higher than the released one
    (285356032), so every TS0726 3- and 4-gang user would be offered the dev
    build.
  - The `-from_tuya` entries (file version `0xFFFFFFFF`, stock image type
    54179) would also be offered to stock Tuya devices.
  - The commit also deletes `main`'s released `*8542fc05*` artifacts for
    TS0726 3-gang and 4-gang.
- **Fix:** restore `zigbee2mqtt/ota/` and `bin/` to their `main` contents:
  `git checkout main -- zigbee2mqtt/ota bin`, then remove the untracked
  `*538f75d3*` files. Commit this as its own commit. Never commit dev builds or
  index entries on a feature branch again; the release process produces them.
- **Status:** verified.

### H0.2 Do not merge or push this branch to the upstream PR

Merging, or pushing the branch where Z2M's OTA index can see it, before every
critical item is fixed would ship broken firmware.

## 4. Step 1 — make the stub model the real platforms

Every item here closes a gap that let a critical bug pass the tests. Do these
first; each makes at least one finding below testable.

### S1. REPL line limit breaks a test — test harness bug

- **Where:** `src/stub/simple_repl.c:12` (`SIMPLE_REPL_MAX_LINE 256`); the
  test is `tests/test_management_clusters.py:81`
  (`test_config_rejects_truncated_or_embedded_nul_raw_records_without_reboot`).
- **Problem:**
  - The test sends a `zcl_write_raw` command of about 290 characters, so
    `fgets` splits it. The tail runs as a separate "unknown command", and every
    later response is out of sync by one.
  - The test cannot have passed anywhere.
- **Fix:**
  - Raise the limit to at least 1024.
  - Make an over-long line fail loudly: read until the newline, then return
    `RES ERR line_too_long`, never a split command.
  - Add a harness self-test that sends a 600-character command.
- **Status:** verified (reproduced).

### S2. Stub notify must honour `HAL_ATTR_REPORT`

- **Where:** `src/stub/hal/zigbee.c:169` (the attribute-changed notify path).
- **Problem:** Telink (`src/telink/hal/zigbee_zcl.c:345`) and Silabs
  (`src/silabs/hal/zigbee.c:212-215`) drop notifications for attributes
  without `HAL_ATTR_REPORT`. The stub does not, which hides C1.
- **Fix:** apply the same early return in the stub. Emit report events only
  for reportable attributes. Reject `configure_reporting` for non-reportable
  attributes with `UNREPORTABLE_ATTRIBUTE`, as the real SDKs do.

### S3. Stub must emulate Telink write ordering

- **Where:** `src/stub/hal/zigbee.c:413` (it copies the candidate into storage
  before COMMIT).
- **Problem:** the patched Telink SDK calls VALIDATE then COMMIT **before**
  `zcl_setAttrVal` copies the value; see C2. The stub copies first, which
  hides C2.
- **Fix:** the decision is C2's. Pick one cross-platform contract ("COMMIT
  receives the candidate and must use it, storage is updated afterwards", or
  the reverse), document it in `src/hal/zigbee.h`, and make the stub follow
  that same order. A test must fail if a COMMIT callback reads the cluster
  storage field instead of the candidate.
- **Recommended contract:** COMMIT receives the candidate bytes and is the
  only place that applies them. The HAL copies to attribute storage after
  COMMIT succeeds. Callbacks never read the attribute storage for the new
  value.

### S4. Stub must enforce Telink and Silabs descriptor capacities

- **Where:** stub registration in `src/stub/hal/zigbee.c`; the limits are
  - `ZCL_CLUSTER_NUM_MAX = 32` in `src/telink/configs/stack_cfg.h:23` (counted
    over `zcl_registerCluster` calls, which excludes OTA);
  - Telink `MAX_ENDPOINTS = 8` and `MAX_ATTRS = 160`;
  - `DEVICE_COMPOSITION_MAX_ENDPOINTS = 10` in
    `src/device_config/device_composition.h:16`.
- **Problem:** the stub accepts any graph the composition accepts, so C3 is
  invisible.
- **Fix:**
  - Put the platform limits in one shared header used by the composition
    preflight, the stub and Telink.
  - The stub fails registration exactly where Telink would.
  - Add a stub command that prints the registration count per platform limit.

### S5. Stub `hal_millis()` must be able to wrap the way Telink does

- **Where:** `src/stub/hal/timer.c`, against `src/telink/hal/timer.c:7-11`.
- **Problem:** on Telink, `hal_millis()` is `clock_time()/16000`. The 32-bit
  16 MHz tick wraps every ~268 s, so the millisecond value wraps at
  `0x10000000` (268 435 ms), not at 2³². The stub never wraps, which hides C8.
- **Fix:**
  - Add a stub option or REPL command to start virtual time near the wrap
    point.
  - Also emulate the Telink wrap modulus, so tests can cross a 268 435 ms
    boundary. After C8 is fixed, the same tests must pass with a full 32-bit
    wrap.

### S6. Stub edge-queue overflow and delayed edge timestamps

- **Where:** `src/stub/hal/gpio.c`, `src/base_components/gpio_edge_queue.c`.
- **Problem:** the stub cannot fill the 32-entry queue before the worker runs.
  It also cannot model Telink stamping a deferred edge with the time
  `hal_gpio_process_pending()` runs. That hides H2 and H3.
- **Fix:** add REPL commands:
  - to inject a burst of N edges without running the worker;
  - to inject an edge whose recorded timestamp lags its real level change by
    a given delay.

### S7. Fix the `commands.c:332` warning

Remove the always-false comparison, or compare against a constant that can
actually fail. The build must have no new warnings.

## 5. Step 2 — tests that must fail before the fixes

Add these before changing firmware behaviour. Each maps to a finding.

| Test | Finding | Expectation |
| --- | --- | --- |
| Relay toggled by a local press emits a report event for OnOff on its endpoint | C1 | report event present |
| Multistate PresentValue, cover lift %, battery voltage/percentage are reportable and emit reports | C1 | reports present |
| Write switch mode to momentary over ZCL → behaviour switches immediately and survives reboot | C2 | momentary behaviour after write and after reboot |
| `SWITCH_BSEED_TS0726_4GANG` config registers within Telink limits; relay 4 OnOff works | C3 | registration succeeds, EP8 toggles |
| NVM fixture from `main` (migration version 1, custom config string, detached switch, startup=previous, multi-press count 0, cover reversal) → boot this firmware | C4 | all settings preserved or mapped |
| Basic `0xFF00` read/write and `0xFF01`/`0xFF02` behave as on `main` (if D1 = keep) | C5 | same ids, types, semantics |
| 0xFC01 write with manufacturer code `0x125D` and with none | C6 | accepted |
| 50 Hz square wave (10 ms half-periods) on a toggle-mode input for 1 s | C7 | relay does not toggle; no reset gesture |
| Press starting 5 ms before the Telink `hal_millis` wrap, released after 50 ms | C8 | short press, no hold/reset |
| Saved relay record with interlock group 0, then config adds `K` for those relays | C9 | interlock from config applies; both relays never on |
| Edge burst overflowing the queue, ending with a release | H2 | button converges to released |
| Deferred-timestamp glitch (5 µs spike stamped 20 ms late) | H3 | no press |
| Switch endpoints' OnOff/Level and cover-switch WindowCovering are client role | H4 | descriptor matches `main` |
| Interrupted cover move, then OPEN | H6 | runs the full travel time; confidence not KNOWN until an endpoint is reached |
| z2m cover position convention (adapter conversion, both directions) | H7 | 0 closed / 100 open as shown to the user |
| Oversized config | H8 | falls back to recovery graph with EP1 management and OTA |

## 6. Step 3 — fixes

### Critical — breaks physical devices or user setups

#### C1. Relay state, button actions, cover position and battery are never reported — verified

- **Where:**
  - attributes without the flag: `src/zigbee/relay_cluster.c:99` (OnOff is
    `HAL_ATTR_READ` only), `src/zigbee/switch_cluster.c:225` (Multistate
    PresentValue), `src/zigbee/cover_cluster.c:172` (lift percentage),
    `src/zigbee/battery_cluster.c:33`;
  - where reports are dropped: `src/telink/hal/zigbee_zcl.c:303` (reportable
    access is set only when `HAL_ATTR_REPORT` is present) and `:345`;
    `src/silabs/hal/zigbee.c:212-215`.
- **Problem:** `main` marked every Telink attribute reportable. This branch
  made the flag explicit, but only 0xFC10/0xFC11 attributes set it.
- **Scenario:** a wall press switches the relay, but Z2M, ZHA and HOMEd never
  see it. Button actions stop. Z2M "configure" fails with an unreportable
  attribute.
- **Fix:**
  - Add `HAL_ATTR_REPORT` to every attribute `main` reported and the
    integrations bind or configure:
    - OnOff `0x0000`;
    - Multistate PresentValue;
    - Window Covering current lift percentage, plus any other position or
      moving attributes the templates configure;
    - battery voltage and percentage;
    - any custom attribute the templates call `configureReporting` on (grep
      the templates for `reporting`).
  - Do not mark `uptime_ms` reportable (see M7).
- **Test:** C1 rows in Step 2, after S2.

#### C2. Telink applies attribute writes one step late — verified

- **Where:** `src/telink/patch_sdk/zcl_pre_write.patch` hunk 1 (pre-write runs
  at the top of `zcl_attrWrite`, before `zcl_setAttrVal`) and
  `src/telink/hal/zigbee_zcl.c:205-218` (COMMIT runs inside pre-write).
  Callbacks then read cluster storage, not the candidate:
  - `src/zigbee/switch_cluster.c:169` → `switch_cluster_on_write_attr` (`:544`
    reads `cluster->relay_index`);
  - `src/zigbee/relay_cluster.c:60`;
  - `src/zigbee/cover_switch_cluster.c:413`.
- **Problem:** on Telink, COMMIT runs before the SDK copies the value into RAM.
  The callback applies and saves the old value; afterwards the SDK writes the
  new value into the attribute.
- **Scenario:** set switch mode to momentary. The device keeps the old mode,
  saves the old mode to NVM, and reads back "momentary". After a reboot it is
  the old mode again. Writing a second time appears to fix it. Every writable
  attribute is affected: relay startup mode, indicator mode, long press,
  binding mode, cover-switch settings, device config.
- **Fix:** implement the contract chosen in S3 on all three backends.
  - Telink option A (recommended): COMMIT callbacks take the candidate bytes
    and write them into the cluster field themselves, then apply and persist.
  - Telink option B: move the COMMIT call after `zcl_setAttrVal` in the patch.
    This needs a second hook point, and it fails the "reject before mutation"
    goal for undivided writes, so A is simpler.
  - Review every `*_attr_write` callback for reads of storage during COMMIT.
- **Test:** C2 row; must fail with the S3 stub before the fix.

#### C3. TS0726 4-gang exceeds the Telink SDK cluster table — verified (count from stub; exact registration count to confirm)

- **Where:** `src/telink/configs/stack_cfg.h:23` (`ZCL_CLUSTER_NUM_MAX 32`).
  `zcl_registerCluster` (`telink_tools/sdk/zigbee/zcl/zcl.c:184`) returns
  `ZCL_STA_INSUFFICIENT_SPACE`, and `zcl_register` ignores it.
  `descriptor_graph_is_valid` (`src/telink/hal/zigbee_zcl.c`) checks a
  64-cluster budget, so it never catches this.
- **Problem:** the stub reports 36 clusters for the 4-gang config: EP1 9, EP2–4
  5 each, EP5–8 3 each. Excluding OTA, about 35 go through
  `zcl_registerCluster`. `main` had about 29. The extra ones come from the
  per-switch 0xFC02 button-event cluster and the EP1 management clusters.
- **Scenario:** clusters past the 32nd silently fail to register. That is
  EP8's relay (OnOff, Level, Groups), so relay 4 stops responding to Zigbee.
  The 4-gang TS0726 is the refactor's primary target device.
- **Fix:**
  1. Count registrations exactly; add the S4 command or a Telink build-time
     check.
  2. Choose one:
     - raise `ZCL_CLUSTER_NUM_MAX`, and measure the RAM cost
       (`zcl_vars.clusterList` entries);
     - drop clusters, e.g. one shared 0xFC02 on EP1 carrying the button
       index instead of one per switch endpoint;
     - drop the per-switch-endpoint Level client if unused.
  3. Make the composition preflight use the real platform limits (S4), so an
     over-limit graph falls back to the recovery graph (H8), never a silent
     partial registration.
  4. Check every `zcl_registerCluster` return value in
     `src/telink/hal/zigbee_zcl.c` and fail to recovery on error.
- **Test:** C3 row.

#### C4. OTA from released firmware wipes the device config and user settings — verified

- **Where:** `src/device_config/nvm_migrations.c:76-96`. `main` stores
  migration version 1, so every step from 2 to 9 runs on the first boot after
  OTA:
  - `delete_relay_configs()`: relay startup mode, indicator modes, inching;
  - `delete_switch_configs()`: switch mode, relay mode (detached), relay
    index, long press, binding mode, cover-switch settings;
  - `capability_state_clear_owned()`, `cover_controller_clear_owned()`: cover
    calibration and position;
  - the `MANAGEMENT_SERVICE_VERSION` step deletes `NV_ITEM_DEVICE_CONFIG`
    (item 2, the user's config string), `NV_ITEM_BASIC_CLUSTER_DATA` (network
    LED) and `NV_ITEM_MULTI_PRESS_RESET_COUNT`. This happens before
    `device_config_service_init()` (`src/app.c:61-62`,
    `src/device_config/device_config_service.c:41-53`), the only code that
    would have carried them over.
  - `main`'s `NV_ITEM_COVER_CONFIG` (motor reversal) is never read.
- **Problem:** the capability plan's compatibility policy ("changed owner
  layouts are cleared, never translated", `capabilities/01_architecture.md:44-55`)
  contradicts `AGENTS.md`, which guarantees OTA updates from released firmware.
- **Scenario:**
  - A user with a corrected pinout or bistable relays loses the custom config
    string and boots the board default. Wrong pins on non-default hardware.
  - Detached-mode users get relays switching again on button presses, which
    cuts power to their smart bulbs.
  - Users who disabled the multi-press reset (count 0) are back to 10: ten
    quick presses factory-reset the device off the network.
  - Startup mode "previous" or "on" is lost, so after a power cut relays stay
    off.
  - Reversed-wiring covers swap open and close.
- **Fix:**
  - Migrations from released versions (1) convert, not delete:
    - item 2 → the new device-config record;
    - multi-press reset count and network LED → the new service record;
    - `main` relay and switch records → the new owner records, field by
      field;
    - `NV_ITEM_COVER_CONFIG` → cover settings.
  - Migrations between unreleased intermediate versions (2–8) may still clear.
    Only version 1 exists in the field; confirm with the release tags.
  - Remove the redundant `else if (oldVersion < INTERLOCK_CONFIG_VERSION)`
    branch.
  - Remove the wording in `capabilities/01_architecture.md` that permits
    dropping released settings (decision D2).
  - `src/silabs/config/silabs_config.h` hard-codes migration version 7 while
    `NVM_MIGRATIONS_VERSION` is 9. Make every platform take the value from the
    `NVM_MIGRATIONS_VERSION` file, as the stub Makefile and Telink already do.
    Otherwise a build that stores 7 would rerun later steps on the next
    update.
- **Test:** C4 row. Build an NVM fixture in the exact byte layout `main`
  writes; take struct layouts from `git show main:src/...`.

#### C5. Management attributes moved off Basic; integrations can no longer reach them — verified (decision D1)

- **Where:** `src/zigbee/basic_cluster.c` lost `0xFF00` (device config, long
  char string, writable), `0xFF01` (status LED, boolean, writable) and
  `0xFF02` (multi-press reset count, uint8, writable); see
  `git show main:src/zigbee/basic_cluster.c:84-93`. They moved to 0xFC10 under
  manufacturer code `0x1A2B` (`src/zigbee/management_protocol.h`), and the LED
  attribute became a three-value enum.
- Diagnostics moved from Basic `0xFF10–0xFF23` to 0xFC11 `0x0000–0x0051`.
- Templates still use the old ids:
  - `helper_scripts/templates/switch_custom.js.jinja:147-290, 550-572`;
  - `helper_scripts/templates/zha_quirk.py.jinja:69-83`;
  - the HOMEd generator.
  - `management_protocol.js.jinja` and `.py.jinja` are generated but unused.
- The generated outputs were never regenerated.
- **Scenario:** after the OTA (which also wipes the config, C4), nobody can
  read or change the config string, LED mode or reset count from Z2M, ZHA or
  HOMEd. Recovery needs raw manufacturer-specific frames or a reflash.
- **Fix (default for D1: keep the released interface):**
  - Keep `0xFF00`, `0xFF01` and `0xFF02` on Basic, with `main`'s types and
    semantics. They are the external interface `AGENTS.md` protects.
  - Back them with `device_config_service`; validate before commit, then
    reboot.
  - Keep 0xFC11 diagnostics if wanted (new and additive), or restore Basic
    `0xFF10+`.
  - If 0xFC10 stays, it must not be the only path. Templates must keep working
    against both old and new firmware, which the Basic attributes already
    guarantee.
  - Regenerate z2m, ZHA, HOMEd and docs in a separate commit.
- **Test:** C5 row.

#### C6. Manufacturer code changes break existing 0xFC01 writes and 0xFC02 — reported

- **Where:** `src/zigbee/cover_switch_cluster.c:471` and
  `src/zigbee/button_event_cluster.c:199` set
  `ZIGBEE_MANAGEMENT_MANUFACTURER_CODE` (`0x1A2B`). `main` registered all
  clusters with manufacturer code 0 (`git show main:src/telink/hal/zigbee_zcl.c:206`).
  Telink pre-write requires an exact manufacturer match
  (`src/telink/hal/zigbee_zcl.c:190-195`). The Z2M template sends `0x125D` for
  0xFC01 (`switch_custom.js.jinja:512`) and nothing for 0xFC02. The ZHA
  quirk's 0xFC02 attributes are `is_manufacturer_specific=False`.
- **Scenario:** cover-switch settings writes return UNSUPPORTED_ATTRIBUTE
  after the update.
- **Fix:**
  - Restore 0xFC01 to `main`'s behaviour: accept the frames today's released
    templates send, with `0x125D` and without a code.
  - Pick one manufacturer-code policy for 0xFC02 and make templates and
    firmware agree.
  - Confirm whether `0x1A2B` is a placeholder (L6).
- **Test:** C6 row.

#### C7. Default debounce dropped from 50 ms to 8 ms — verified

- **Where:** `src/device_config/config_parser.c:6`
  (`CONFIG_DEFAULT_DEBOUNCE_MS 8`) and `src/base_components/button_input.h:9`.
  On `main` it was `DEBOUNCE_DELAY_MS 50` (`main:src/base_components/button.h:11`).
  No `device_db.yaml` entry sets a `D<N>` token, so every device changes.
- **Problem:** the new rule commits any level held for 8 ms. Mains-frequency
  pickup on long switch wiring (the known "ghost switching" case) has 10 ms or
  8.3 ms half-periods, so each one becomes an edge. `input_pipeline/DECISIONS.md`
  items 5 and 18 already call 8 ms provisional, pending the hardware stage
  that never ran.
- **Scenario:** toggle-mode inputs (no `M` token, most devices) chatter the
  relay at mains frequency. Ten clicks under 350 ms apart trigger the
  multi-press factory reset in well under a second.
- **Fix:**
  - Restore a 50 ms default until a hardware measurement justifies less.
  - Consider rate-limiting relay toggles from local input as defence in depth.
- **Test:** C7 row.

#### C8. Telink `hal_millis()` wraps at 268 435 ms; the new pipeline assumes a 2³² wrap — verified

- **Where:** `src/telink/hal/timer.c:7-11`; consumers that assume wrap-safe
  `now - then`:
  - `src/base_components/gesture_fsm.c:100-104` (hold elapsed);
  - `src/base_components/button_input.c:96` (debounce elapsed);
  - timer remaining time;
  - HOLD_END durations;
  - `src/base_components/cover_controller.c` elapsed travel;
  - relay inching deadlines;
  - interlock dead time.
- **Problem:** across the ~268 s wrap, `now - then` produces a value near 2³².
- **Scenario:** a short tap starting just before the wrap reads as a hold that
  is already complete. On the onboard button, hold means factory reset. The
  same wrap breaks debounce, cover position and timer deadlines once every
  4.5 minutes.
- **Fix:** make `hal_millis()` a true 32-bit millisecond counter. Extend
  `clock_time()` with a software overflow count updated at least once per wrap
  period: from the main loop, plus a periodic timer for sleepy end devices.
  Then all unsigned arithmetic is correct. Do not patch individual consumers.
- **Test:** C8 row, after S5.

#### C9. Saved relay record overrides the configured interlock group — verified

- **Where:**
  - `src/zigbee/relay_cluster.c:84-89`: the group comes from the config, then
    `relay_cluster_load_attrs_from_nv` overwrites it, then
    `relay_ctrl_set_interlock_group` applies it;
  - `src/zigbee/relay_cluster.c:315-323`: any relay attribute write persists
    `interlock_group`;
  - `src/device_config/device_config_service.c:88-123`: a config change only
    reboots and never clears owner records.
- **Problem:** the persisted value wins over the hardware config. The writable
  interlock attribute also accepts any id:
  - ids above the configured groups map to cover groups
    (`src/device_config/feature_wiring.c:251-266`);
  - a new id creates a group with no dead time.
- **Scenario:**
  1. A user sets a startup mode on a 2-relay module, which saves group 0.
  2. They later add `K` to the config to interlock a motor or two-direction
     load.
  3. After reboot the saved 0 removes both relays from the group. Both can be
     energized together, including at boot with startup = ON.
- **Fix:**
  - Treat the config-string interlock as authoritative. Persisted interlock
    applies only when the config defines none; or remove the writable
    interlock attribute entirely if the owner agrees.
  - Restrict writes to groups defined by the config, never cover groups.
  - Clear relay owner records when the config string's relay topology changes.
- **Test:** C9 row.

### High

#### H1. The Telink SDK patch can be skipped or misapplied silently — reported

- **Where:** `src/telink/patch_sdk/zcl_pre_write.patch` and the patch rule in
  `src/telink/sdk.mk`.
- **Problem:**
  - The patch is malformed: its context lines use 2-space indentation where
    the SDK uses 4, and the hunk line numbers are inconsistent. It only
    applies with fuzz 2; with `-F0`, 2 of 10 hunks fail.
  - The rule writes the output in place with no `.DELETE_ON_ERROR` and no
    temp-and-rename. A failed `patch` leaves a newer, unpatched or
    half-patched `build/.../sdk-src/zigbee/zcl/zcl.c`, and the next `make`
    compiles it.
  - Firmware built that way has no write validation, and it still links,
    because `hal_zigbee_telink_pre_write` is defined but unused.
- **Fix:**
  - Regenerate the patch against the exact SDK file (`diff -u`).
  - Apply with `patch -F0 --forward` into a temp file, then rename.
  - Add a post-check that greps the patched source for `zcl_incoming_write`
    and fails the build otherwise.
  - Add `.DELETE_ON_ERROR:`.
  - Consider avoiding the SDK patch entirely if the write hook can be done
    through the SDK's own callbacks.
- **Test:** run the Telink build from a clean `build/`, then corrupt the patch
  and confirm the build fails.

#### H2. Edge queue overflow leaves a button stuck — reported

- **Where:** `src/base_components/gpio_edge_queue.c:15-18` (a failed push
  returns and the sink ignores it), `src/base_components/button_input.c:111-114`,
  `src/silabs/hal/gpio.c:71-83` (`last_level` is updated before the push).
  Telink can push up to 50 re-arm passes of edges per process call.
- **Scenario:** a bouncy contact fills the 32-entry queue and the final
  release edge is dropped. The button stays logically DOWN:
  - the hold timer fires (the relay toggles in long-press mode, or a
    continuous level-move goes to bindings);
  - the next real press is ignored as a repeat of the current state.
- **Fix:** on overflow, set a resync flag. The worker then samples every
  watched pin and injects a synthetic edge wherever the level differs from
  `button_input`'s committed state.
- **Test:** H2 row (needs S6).

#### H3. Telink stretches short glitches into presses — reported

- **Where:** `src/telink/hal/gpio_interrupts.c:114-144`. The interrupt handler
  records the first edge with an accurate time, then disables watched pin
  interrupts until `hal_gpio_process_pending()` runs in the main loop. The
  return edge is stamped with the processing time, not the time it happened.
- **Scenario:** a microsecond spike during a main-loop stall of 8 ms or more
  (flash erase or NVM write, stack processing, UART) becomes a press lasting
  the whole stall. A relay switching an inductive load can cause both the
  spike and the NVM-write stall. `main` re-read the pin about 3 ms after the
  interrupt, which filtered such spikes.
- **Fix:** keep pin interrupts enabled with polarity flipping, so every edge
  gets an interrupt-time stamp. Alternatively, when the worker sees a level
  that differs from the last captured edge, stamp the synthetic edge as
  "unknown time" and require a full debounce interval from processing time.
- **Test:** H3 row (needs S6).

#### H4. Telink edge queue has two writers — reported

- **Where:** `src/telink/hal/gpio_interrupts.c:133-134` and `:206`.
  `capture_snapshot()` runs in main context after `enable_watched_irqs()`, and
  in `telink_gpio_reinit_interrupts`, while the interrupt handler can run the
  same function. `gpio_edge_queue_push` is single-producer
  (`input_pipeline/02_gpio_edge_capture.md`).
- **Scenario:** an interleaved push overwrites the interrupt handler's slot and
  loses the edge. `last_level` was already updated, so the button sticks as in
  H2. Rare (a microsecond window), but permanent until the next edge.
- **Fix:** wrap main-context captures in `drv_disable_irq()` /
  `drv_restore_irq()`. With H2's resync in place, the outcome becomes
  self-healing.

#### H5. Switch and cover-switch endpoints advertise outgoing-command clusters as servers — verified

- **Where:** `src/zigbee/switch_cluster.c:213,240` and
  `src/zigbee/cover_switch_cluster.c:476` set `HAL_ZIGBEE_ROLE_SERVER`. `main`
  used `is_server = 0` (`main:src/zigbee/switch_cluster.c:133,166`).
- **Scenario:** Z2M and ZHA see phantom OnOff, Level and WindowCovering
  servers on button endpoints. Binding from those endpoints may be refused or
  misrouted (unverified). The phantom servers also count against the C3
  cluster limit.
- **Fix:** use `HAL_ZIGBEE_ROLE_CLIENT` for every cluster the endpoint only
  sends. Check every `.role` assignment against `main`'s `is_server`.
- **Test:** H5 row (descriptor snapshot compared with `main`).

#### H6. After an interrupted move, the cover runs too short and then claims a known position — reported

- **Where:** `src/base_components/cover_controller.c:189-199`, `:208`,
  `:224-240`; `:272` and `:397` (stop from an unknown state upgrades
  confidence).
- **Scenario:**
  1. Power is lost mid-move. The cover reloads as "position unknown" but keeps
     the stale stored position.
  2. The next OPEN is timed from that stale position, e.g. a stored 90% when
     the cover is actually at 0%. It runs about 10% of the travel time plus
     overrun.
  3. It then marks the cover fully open with KNOWN confidence, and every later
     go-to is offset.
- **Fix:**
  - When confidence is unknown, endpoint moves run the full travel time plus
    overrun, and only reaching an endpoint sets KNOWN.
  - Reject or refuse GOTO while the position is unknown.
  - STOP never raises confidence.
- **Test:** H6 row.

#### H7. Cover position convention vs the Z2M template — reported, needs checking against zigbee-herdsman-converters

- **Where:** `src/zigbee/cover_cluster.c:135-139`,
  `src/base_components/cover_controller.c:378`,
  `helper_scripts/templates/switch_custom.js.jinja:595-598`
  (`coverInverted: true`).
- **Problem:** the adapter now reports the standard ZCL convention (0 = fully
  open). With `coverInverted: true`, Z2M probably treats the raw value as
  "% open", so position is shown inverted and go-to commands are inverted.
  Unknown or manual-mode position reports `0xFF`, which Z2M will show as a
  bogus number.
- **Fix:**
  - Read how `coverInverted` is used in `windowCovering` (herdsman
    converters).
  - Make the firmware wire convention and the template flag agree, matching
    what `main` sent on the wire for the same physical position.
  - Report unknown position in a way Z2M ignores, or don't report it.
- **Test:** H7 row (wire value at open, closed and 30% open, compared with
  `main`).

#### H8. A config that passes preflight can leave the device unreachable — reported

- **Where:** `src/device_config/device_composition.h:16` (10 endpoints);
  `src/device_config/feature_wiring.c:323` (preflight checks cluster slots
  only, not `MAX_ATTRS` 160 or `MAX_ENDPOINTS` 8);
  `src/telink/hal/zigbee_zcl.c:228-246` (on validation failure it returns
  before `zcl_init`, with no recovery fallback); `src/silabs/hal/zigbee.c:133`.
- **Scenario:** a user config with 5 switches and 5 relays on an end device,
  or 10 endpoints. The device joins the network with no endpoints, no OTA and
  no management cluster, and cannot be fixed over Zigbee. Only a reflash
  recovers it.
- **Fix:**
  - Preflight uses the shared platform limits (S4).
  - On any HAL registration failure, rebuild the recovery graph (EP1: Basic
    with config attributes, management, OTA) and register that.
  - The recovery graph must always fit.
- **Test:** H8 row.

#### H9. Silabs build has never compiled; management clusters are probably unreachable — unverified

- **Where:** `src/silabs/hal/zigbee.c:13` (clusters are built in
  `clusters_buffer`), `src/silabs/custom-zcl.xml`,
  `src/silabs/slcp_files/*.slcp`, and `src/silabs/Makefile:121` (the
  `slc configuration` lines were removed).
- **Problem:**
  - For clusters at 0xFC00 and above, `sli_zigbee_af_match_cluster` looks up
    manufacturer codes in the ZAP-generated table, indexed by
    `cluster - generatedClusters`. The HAL's own buffer makes that index
    meaningless.
  - `zcl_config.zap` does not contain 0xFC10/0xFC11.
  - `custom-zcl.xml` declares the config as a non-writable short string, while
    runtime registers it as a writable long string.
- **Scenario:** every manufacturer-specific attribute (config, diagnostics,
  button events) returns UNSUPPORTED on EFR32. A fresh `make setup` may fail.
- **Fix:**
  - Restore a working `make silabs/build` first; that is a precondition for
    every Silabs claim.
  - Align `custom-zcl.xml` and the ZAP file with the runtime metadata.
  - Verify manufacturer-specific matching on hardware or in a Silabs
    simulator.
  - Until then, list Silabs as unverified in every summary and do not ship
    Silabs images from this branch.

#### H10. Flash headroom is thin — reported

- **Where:** the TS0726 4-gang `.bin` is 200 405 bytes, up from 180 941 on
  `main` (+19.5 KB). The application OTA slot is 0x34000 = 212 992 bytes
  (`telink_tools/sdk/proj/drivers/drv_nv.h:197,225`), leaving ~12.6 KB.
- **Problem:** if stock Tuya firmware uses the SDK's bootloader layout
  (0x31000 = 200 704 bytes, `drv_nv.h:230`), the `-from_tuya` image has
  ~300 bytes to spare. Whether stock firmware uses that layout is unverified.
- **Fix:**
  - Record the image size of the largest image per board in `STATE.md`, and
    add a build-time size check against the smallest slot the image can be
    installed into.
  - Recover space by removing the dead code (L1, L2).

### Medium

- **M1. OnWithTimedOff ignores "accept only when on"** —
  `src/zigbee/relay_cluster.c:192-205`. `payload[0]` bit 0 is ignored, so a
  command meant only to extend an already-on relay switches on an off mains
  load. `on_time == 0` turns the relay on, then off on the next tick. Follow
  ZCL 3.8.2.3.6. Reported.
- **M2. Switch-to-relay mapping uses the endpoint index as a relay id** —
  `src/zigbee/switch_cluster.c:268,339,383` with
  `src/device_config/config_parser.c:237,258`. `relay_index - 1` is validated
  as a relay-endpoint index but used as a relay id. If a `C` (cover) token
  comes before the `R` tokens, switch 1 drives the cover's open relay. No
  `device_db.yaml` entry mixes them today; custom configs can. Reported.
- **M3. Toggle during interlock dead time re-requests ON** —
  `src/base_components/relay_controller.c:115-116`. While a relay waits for
  the dead time, `is_on` is still false, so a second toggle requests ON again
  instead of cancelling. The user presses twice expecting off, and the relay
  comes on. Toggle must take the pending target into account. Reported.
- **M4. Stale interlock resume state** — `src/base_components/interlock.c:28-33,126-129`
  and `src/base_components/relay_controller.c:137-139`. `resume_relay` is set
  before submit and not cleared when submit returns BLOCKED. The next ON for
  that relay then skips turning its peers off, so two interlocked relays can
  both be on. This becomes live once something calls `relay_ctrl_inhibit`
  (planned protection). Clear it on every path. Reported.
- **M5. Inching applies only to the Zigbee On command** —
  `src/zigbee/relay_cluster.c:172`. Zigbee Toggle, MoveToLevelWithOnOff, local
  switch toggle/on and startup = ON all use plain ON, so an impulse relay (a
  garage door) stays energized. This matches `input_pipeline/07_relay_controller.md`
  but is a hazard. Move inching into `relay_controller` so every ON source
  gets it. Reported.
- **M6. Rejected cover writes still report success** —
  `src/zigbee/cover_cluster.c:82-115`. VALIDATE only checks length.
  Out-of-range deadzone or travel-time writes are acknowledged, then silently
  reverted without a report. Range checks belong in VALIDATE. The new
  open/close time, deadzone and overrun attributes are also missing from the
  Z2M and ZHA templates; mirror them (`AGENTS.md` rule). Reported.
- **M7. Diagnostics work on every loop** — `src/app.c:126-128` re-sets the
  diagnostics provider and copies about 76 bytes twice per main-loop
  iteration. That costs CPU and battery on end devices. `uptime_ms` is
  reportable and changes constantly. Refresh on demand (on read) and drop
  `uptime_ms` reportability. Reported.
- **M8. Write parsing hardening** — `zcl_parseInWriteCmd` (SDK) never checks
  string lengths against `dataLen`, so it can over-read up to 255 bytes past
  the frame. Pre-write does not check `candidate_len <= attribute->size`, so a
  writable string attribute without a callback would overflow in
  `zcl_setAttrVal`. None exists today. Add both checks in the pre-write hook.
  Reported.
- **M9. Undivided writes rejected for every attribute** —
  `src/telink/hal/zigbee_zcl.c:182`. `ZCL_CMD_WRITE_UNDIVIDED` now returns
  UNSUPPORTED_ATTRIBUTE, including for standard attributes. Either support it
  (validate all records, then commit all) or document it as unsupported.
  Reported.
- **M10. Silabs sleepy end device may not wake on button press** —
  `src/silabs/hal/gpio.c:64-86`. `main`'s interrupt handler called
  `sl_zigbee_af_event_set_active`; the new one only sets a flag polled by
  `app_task`. If the power manager goes back to sleep after the interrupt, the
  press is handled late. Unverified; restore an event activation from the
  interrupt handler, which is allowed and only schedules work.
- **M11. Timer pool exhaustion kills button input** —
  `src/base_components/button_input.c:70-76,130-138`. Telink's event-timer
  pool (`TIMER_EVENT_NUM` 24) is shared with the stack. If
  `ev_timer_taskPost` returns NULL, `button_worker_scheduled` stays true for
  good and input is dead until reboot. Neither the HAL nor `timer_service`
  checks this. Check the result, retry from the main loop, and count failures.
  Reported.
- **M12. Debounce can be skipped** — `src/base_components/button_input.c:80-99`.
  `now` is sampled before the queue is drained. An edge pushed during the
  drain gives `now - raw_since_ms = 0xFFFFFFFF` and commits immediately.
  Sample `now` after draining. Reported.
- **M13. Reports from manufacturer-specific clusters** — Telink
  `zcl_sendReportCmd` sends `MANUFACTURER_CODE_NONE`; Silabs sends one
  (`src/silabs/hal/zigbee.c:445`). Make both backends behave the same and
  verify Z2M parses them. Unverified.

### Low

- **L1. Speculative metering code** — `src/base_components/meter_service.c`
  (407 lines), `energy_meter.h`, `meter_fake_driver.*`, `src/hal/gpio_counter.h`
  and three `gpio_counter.c` backends. Every non-stub backend returns "not
  supported". The only production caller is `meter_service_clear_owned` in a
  migration. Under `AGENTS.md` ("no dead symbols") remove it until a real
  meter IC is identified (D4). Also remove NVM items 90+ if nothing writes
  them; ids are never reused, so just stop using them.
- **L2. `action_mapper` has unused features** — only factory-reset mappings
  are installed. Unused: `ACTION_RELAY`, `ACTION_BIND_*`,
  `ACTION_BUTTON_EVENT`, `ACTION_TRIGGER_DOWN`/`UP`, count matching, the
  `binding` and `button_event` sinks, and `ACTION_SYSTEM_REBOOT`. Also unused:
  `hal_gpio_unwatch_pin` (tests only) and `hal_gpio_edge_t.seq`. Remove or
  wire up.
- **L3. `i<image_type>` config token is parsed and discarded** —
  `src/device_config/config_parser.c:172-175`. `hal_zigbee_set_image_type` is
  no longer called; this is a regression from `main`. Restore it.
- **L4. `feature_wiring.c` readability** — lines 50-73, 99-120, 152-162,
  251-266, 282-289 pack several statements per line. They also hard-code array
  sizes (`[11]`, `[10]`, `clusters[50]` against
  `DEVICE_COMPOSITION_MAX_CLUSTERS` 48) and magic numbers (800 ms, 200 ms dead
  time). A computed `left` is discarded. Globals `button_configs` and
  `gesture_configs` shadow file-static arrays of the same names. This is the
  most safety-relevant wiring code; rewrite it one statement per line, with
  named constants.
- **L5. Cover controller details** — `cover_controller.c`:
  - `cover_controller_state()` has side effects and re-enters through the
    observer (`:364-379`, `cover_cluster.c:27-35`);
  - a new GOTO in the same direction stops and restarts the motor, costing a
    200 ms stop, and writes NVM twice (`:414-433`);
  - GOTO to the current position energizes the motor for 0 ms;
  - `elapsed * 10000` overflows past 429 496 ms (`:156`).
- **L6. Manufacturer code `0x1A2B`** looks like a placeholder; confirm it is
  not an assigned code (D3).
- **L7. Interlock defaults** — `K` without `:ms` gives 0 ms dead time (contacts
  can overlap on motor loads), `interlock_add_group` results are ignored, and
  groups are never freed (maximum 13). `capability_state_clear_owned` loops
  over 5 relays, but up to 10 relay records exist.
- **L8. Misc input**:
  - `button_input_set_active_high` re-samples silently, leaving a stale
    gesture state (`button_input.c:166-179`);
  - Telink `gpio_read_all` reads only ports A–D (also on `main`);
  - stale comments that refer to removed APIs in
    `src/silabs/hal/gpio.c:114-116,139-145`;
  - the queue publishes the `volatile` head without a compiler barrier.
- **L9. Layering** — 0xFC10/0xFC11 ids live in the generated
  `management_protocol.h`, not `zcl_ids.h` as `AGENTS.md` requires.
  `meter_service` (base_components) includes `device_config/nvm_items.h`, a
  dependency in the wrong direction.
- **L10. Private cluster with no attributes** — `register_private_cluster`
  (`src/telink/hal/zigbee_zcl.c:44-57`) finds its cluster through the
  attribute-table pointer, so a private cluster with zero attributes takes the
  next cluster's identity. Latent.

## 7. Decisions needed from the owner

| # | Question | Default if no answer |
| --- | --- | --- |
| D1 | Keep Basic `0xFF00`/`0xFF01`/`0xFF02` as the management interface, or move to 0xFC10 with updated converters? | Keep Basic; it is the released interface `AGENTS.md` protects. 0xFC10/0xFC11 may stay only as additive extras. |
| D2 | Replace the capability plan's "clear, never translate" compatibility policy? | Yes: migrate everything from released version 1. Unreleased intermediate versions may clear. |
| D3 | Manufacturer code for private clusters | Keep `main`'s on-wire behaviour for 0xFC01 and 0xFC02; do not introduce `0x1A2B` without a reason. |
| D4 | Freeze capability phases 5–9 (metering, overload, GPIO probe, pin-info, retry caches)? | Freeze. Remove the meter code (L1). Do not build the Zigbee GPIO probe: it pulses arbitrary pins as outputs on mains hardware. |
| D5 | Split the branch? | Yes: (a) input pipeline + relay/interlock + fixes from this document; (b) cover controller; (c) management protocol, only if D1 chooses to move. |

## 8. Exit criteria before any hardware flash

1. Every C and H item is fixed or explicitly deferred by the owner. H9
   (Silabs) may stay deferred, with no Silabs images shipped.
2. `make tests` is green, with the S1–S6 stub changes in place.
3. `BOARD=SWITCH_BSEED_TS0726_4GANG make board/build` passes; image size and
   registration count are recorded.
4. OTA index and `bin/` are identical to `main`.
5. An NVM fixture from `main` passes the C4 test.
6. Then run the deferred hardware stage (`input_pipeline` stage 6) on one
   TS0726 4-gang:
   1. flash `main`; configure detached mode, startup = previous and a custom
      multi-press count;
   2. OTA to this branch;
   3. confirm the settings survive, relay reports arrive in Z2M, all four
      relays respond, and there are no ghost presses on long wiring at 50 ms
      debounce.
