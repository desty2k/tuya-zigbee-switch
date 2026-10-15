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

## 3. Step 0 — product context and housekeeping

### Product context

This branch is a separate, BSEED-focused firmware published from the
`desty2k` fork. It is not merged back into upstream `romasku/tuya-zigbee-switch`
`main`. The fork's own OTA index and `bin/` artifacts are its release channel,
so committing BSEED builds and index entries is intended.

Consequences for this review:

- Compatibility with upstream's Zigbee interface, generated converters or NVM
  layouts is not a requirement. The fork's own templates and generated
  converters must match the firmware it ships.
- "`main`" below means upstream `main`, the base the fork diverged from. It is
  used as the reference for behaviour that was known to work on hardware, not
  as a compatibility target.
- `AGENTS.md` still states upstream's compatibility guarantees (Zigbee
  interface, OTA from released firmware). Update it to describe the fork's
  actual guarantees, so later agents do not flag intended breaks (see D2).

### H0.1 Pin OTA index URLs to an immutable ref — low

- **Where:** `zigbee2mqtt/ota/index_router.json` (4 entries) and
  `zigbee2mqtt/ota/index_router-FORCE.json` (2 entries).
- **Problem:** the URLs use
  `github.com/desty2k/.../raw/refs/heads/refactor/input-pipeline/...`, a
  branch ref. The index also stores the file's hash and size. When the branch
  later points at a different build with the same path, or the file is
  removed, Z2M downloads a file that no longer matches the hash and rejects
  the update.
- **Fix:** generate URLs that use the commit SHA or a release tag.
- **Note:** the `-from_tuya` entries (file version `0xFFFFFFFF`, stock image
  type 54179) offer this build to any stock TS0726 device that uses this
  index. That is the intended conversion path; keep it only for builds that
  passed the exit criteria in section 8.
- **Status:** verified.

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
| NVM fixture with a lower migration version (e.g. upstream `main` = 1, with custom settings) → boot this firmware | C4 | boots safely, owners at defaults, network membership kept |
| Every attribute the generated templates read, write or configure exists in the firmware descriptor with the same cluster, type, access and manufacturer code | C5 | template/firmware parity |
| 0xFC01/0xFC02 writes with the templates' manufacturer code | C6 | accepted; wrong code rejected |
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

#### C4. OTA onto a device running earlier firmware resets its settings — verified, severity medium for the fork

- **Where:** `src/device_config/nvm_migrations.c:76-96`. A device running
  upstream `main` firmware, or an earlier fork build, stores a lower migration
  version, so every later step runs on the first boot after OTA:
  - `delete_relay_configs()`: relay startup mode, indicator modes, inching;
  - `delete_switch_configs()`: switch mode, relay mode (detached), relay
    index, long press, binding mode, cover-switch settings;
  - `capability_state_clear_owned()`, `cover_controller_clear_owned()`: cover
    calibration and position;
  - the `MANAGEMENT_SERVICE_VERSION` step deletes `NV_ITEM_DEVICE_CONFIG`
    (item 2, a custom config string), `NV_ITEM_BASIC_CLUSTER_DATA` (network
    LED) and `NV_ITEM_MULTI_PRESS_RESET_COUNT`.
  - Network membership, binds and groups survive.
- **Context:** the fork does not promise settings continuity from upstream
  firmware (product context in Step 0), so clearing is acceptable. The result
  is the same as a fresh flash with the board's default config.
- **What still matters on your own devices after such an OTA:**
  - Detached mode is gone, so buttons switch the relays again. Relays feeding
    smart bulbs need reconfiguring.
  - Multi-press reset count returns to the default 10.
  - Startup mode returns to the default, so relays stay off after a power cut
    until reconfigured.
  - A custom config string is replaced by the board default. Stock BSEED
    boards are unaffected because their default is correct.
- **Fix:**
  - Document "settings are reset when updating from upstream or pre-refactor
    builds" in `docs/changelog_fw.md`.
  - Remove the redundant `else if (oldVersion < INTERLOCK_CONFIG_VERSION)`
    branch.
  - `src/silabs/config/silabs_config.h` hard-codes migration version 7 while
    `NVM_MIGRATIONS_VERSION` is 9. Make every platform take the value from the
    `NVM_MIGRATIONS_VERSION` file, as the stub Makefile and Telink already do.
    Otherwise a build that stores 7 reruns later clearing steps on the next
    update.
  - From the first fork release onward, decide whether settings must survive
    fork-to-fork OTA (D2). If so, later layout changes need converting
    migrations.
- **Test:** C4 row: an NVM fixture with a lower version boots safely, with
  every owner at defaults, and network membership kept.

#### C5. Firmware and the fork's own converter templates disagree on management attributes — verified

- **Where:** the firmware moved the device config string, status LED and
  multi-press reset count from Basic `0xFF00`/`0xFF01`/`0xFF02` to 0xFC10
  under manufacturer code `0x1A2B` (`src/zigbee/management_protocol.h`). The
  LED attribute became a three-value enum. Diagnostics moved from Basic
  `0xFF10–0xFF23` to 0xFC11 `0x0000–0x0051`.
  - The templates still use the Basic ids:
    - `helper_scripts/templates/switch_custom.js.jinja:147-290, 550-572`;
    - `helper_scripts/templates/zha_quirk.py.jinja:69-83`;
    - the HOMEd generator.
  - `management_protocol.js.jinja` and `.py.jinja` are generated but never
    included.
  - The generated outputs (`zigbee2mqtt/`, `zha/`, `homed/`) were not
    regenerated.
- **Scenario:** a device running this firmware cannot be configured from the
  fork's Z2M/ZHA/HOMEd converters. Config string, LED mode, reset count and
  diagnostics are unreachable. Fixing a wrong config needs raw
  manufacturer-specific frames from the Z2M dev console.
- **Fix (default for D1: 0xFC10/0xFC11 is the interface):**
  - Switch every template to 0xFC10/0xFC11 using the generated
    `management_protocol` fragments; drop the Basic `0xFF0x`/`0xFF1x`
    definitions.
  - Make sure the converters send the manufacturer code the firmware requires
    (D3).
  - Regenerate z2m, ZHA, HOMEd and docs in a separate commit.
  - Add a check (e.g. in `make tools/check_management_protocol`) that fails
    when a template references an attribute the firmware does not declare.
- **Test:** C5 row.

#### C6. Manufacturer codes in firmware and templates disagree — reported

- **Where:** `src/zigbee/cover_switch_cluster.c:471` and
  `src/zigbee/button_event_cluster.c:199` set
  `ZIGBEE_MANAGEMENT_MANUFACTURER_CODE` (`0x1A2B`). Telink pre-write requires
  an exact manufacturer match (`src/telink/hal/zigbee_zcl.c:190-195`). The Z2M
  template sends `0x125D` for 0xFC01 (`switch_custom.js.jinja:512`) and no
  code for 0xFC02. The ZHA quirk marks 0xFC02 attributes
  `is_manufacturer_specific=False`.
- **Scenario:** cover-switch settings and button-event attribute writes from
  the fork's converters return UNSUPPORTED_ATTRIBUTE.
- **Fix:**
  - Choose one manufacturer code for all fork-private clusters (D3), and
    generate it into firmware and every template from
    `helper_scripts/management_protocol.json`.
  - Confirm `0x1A2B` is not an assigned code (L6).
- **Test:** C6 row: writes with the templates' code are accepted, writes with
  a wrong code are rejected.

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

## 7. Owner decisions (answered 2026-10-14)

| # | Decision |
| --- | --- |
| D1 | Management settings live on fork-private clusters 0xFC10/0xFC11. Templates and generated converters follow the firmware (C5). Basic `0xFF0x` is not restored. |
| D2 | No settings continuity from upstream or pre-release fork firmware (documented reset, C4). From the first fork release onward, settings must survive fork-to-fork OTA: layout changes need converting migrations. Recorded in `AGENTS.md`. |
| D3 | One manufacturer code for every fork-private cluster (0xFC01, 0xFC02, 0xFC10, 0xFC11), defined once in `helper_scripts/management_protocol.json` and generated into firmware and templates. Confirm `0x1A2B` is not assigned to another vendor; choose another if it is. |
| D4 | Capability phases 5, 7–9 (metering driver, measurement adapters, overload, profiles) stay frozen until the outlet is characterized. The meter code stays but is compiled only for boards whose `features` include it (D6). Phase 6.3–6.5 is replaced by the reduced GPIO probe in section 10, because the outlet cannot be opened. |
| D5 | Work proceeds in waves (section 9). |
| D6 | Per-board features are selected at build time. `device_db.yaml` gets a `features` list per board; the build compiles only those features; the generator/CI fails when a board's `config_str`/`alt_config_str` uses a token for an unlisted feature; boards without the field derive features from their config tokens. At runtime a token for a feature not in the image boots the recovery graph with a "feature not in this build" error. `category` stays metadata and gains `cover`. |
| D7 | Size optimisation: our own code (`src/` outside the vendor SDK, including `src/telink/`) compiles with `-Os`; the Telink SDK stays at `-O2`. Every Telink object also builds with `-fno-jump-tables`: tc32-gcc 4.5.1 at `-Os` emits byte-relative switch tables but dispatches them as 32-bit absolute addresses, and `helper_scripts/check_tc32_jump_tables.py` fails the link if a malformed dispatch appears. SDK protocol code at `-Os` (~3 KB) is held in reserve. Debounce default is 25 ms (owner decision 2026-10-14). |

## 8. Exit criteria before any hardware flash

1. Every C and H item is fixed or explicitly deferred by the owner. H9
   (Silabs) may stay deferred, with no Silabs images published.
2. `make tests` is green, with the S1–S6 stub changes in place.
3. `BOARD=SWITCH_BSEED_TS0726_4GANG make board/build` passes; image size and
   SDK cluster registration count are recorded in `STATE.md`.
4. Generated converters are regenerated from the current templates and pass
   the C5 parity check.
5. OTA index entries for the build use an immutable URL (H0.1).
6. Then run the deferred hardware stage (`input_pipeline` stage 6) on one
   TS0726 4-gang:
   1. OTA from the firmware the device runs today; confirm it boots, stays
      joined, and every relay responds (including relay 4, C3);
   2. reconfigure via the regenerated Z2M converter: switch mode, detached
      mode, startup mode, multi-press count; confirm each write takes effect
      immediately and survives a power cycle (C2);
   3. confirm relay state reports and button actions arrive in Z2M (C1);
   4. confirm no ghost presses on long switch wiring at the 50 ms debounce,
      and no reset or hold from short taps over at least 10 minutes, which
      crosses the Telink clock wrap twice (C7, C8).

## 9. Delegation plan

Each implementing agent owns a vertical slice: stub fidelity for its area,
failing tests, then the fix. Each works in its own worktree and branch; the
integrator merges. After every wave an independent reviewer reruns all
verification and checks the diff against this document before the merge.
Implementing agents' own reports are not accepted as evidence.

| Wave | Agent | Scope | Depends on |
| --- | --- | --- | --- |
| 0 | integrator | S1, S7, `AGENTS.md` compatibility text, decisions, Telink build host | — |
| 1 | A: Zigbee HAL | S2, S3, S4 → C1, C2, C3, H5, H8, H1, M8, M9 | wave 0 |
| 1 | B: input and timing | S5, S6 → C7, C8, H2, H3, H4, M11, M12 | wave 0 |
| 1 | F: build and platform | H10 size check, C4 (Silabs migration version, changelog), H9 Silabs build | wave 0 |
| 2 | C: relays and interlock | C9, M1, M3, M4, M5, L7 | A merged |
| 2 | D: cover | H6, H7, M6, L5 | A merged |
| 2 | E: integrations | C5, C6, D3, template/firmware parity check, regenerated converters | A merged |
| 1.5 | S: size and features | D6, D7; record per-board sizes; 4-gang OTA file below 200704 bytes with margin | wave 1 merged |
| 2 | P: GPIO probe | section 10 | wave 1.5 merged |
| 3 | cleanup | L1, L2, L3, L4, L8, L9, L10, M2, M7, M10, M13 | waves 1–2 |
| 4 | owner | hardware exit criteria (section 8) | wave 3 |

### Telink build host

The Telink toolchain runs on `sk8` (x86_64 Ubuntu), not on macOS. Sync the
worktree there and build:

```bash
rsync -az --delete --exclude telink_tools --exclude silabs_tools --exclude build \
  --exclude stub_nvm_data --exclude .venv ./ sk8:Projects/tuya-zigbee-switch/
sk8exec 'cd ~/Projects/tuya-zigbee-switch && export PATH=$HOME/.local/bin:$PATH && \
  BOARD=SWITCH_BSEED_TS0726_4GANG make board/build'
```

`telink_tools` is installed there and `yq` (mikefarah v4) is in
`~/.local/bin`. Use a separate remote directory per worktree when agents build
in parallel. Do not commit the `bin/` output of verification builds.

Baseline at `1f1d1ac4` (wave 0 start): `BOARD=SWITCH_BSEED_TS0726_4GANG make
board/build` passes; 199760 text / 645 data / 39302 bss; `.bin` 200405 bytes;
existing `hal/system.c:12` noreturn warning. The SDK patch applied with fuzz 2
on hunks 3 and 7 and fuzz 1 on hunks 4 and 8 (confirms H1).

### Size baseline (2026-10-14)

Measured on `fix/wave1-a-zigbee-hal` (wave 1 complete), `SWITCH_BSEED_TS0726_4GANG`:

| Variant | OTA file | Headroom vs 212992 / 200704 |
| --- | --- | --- |
| `-O2` everywhere (current) | 204642 | 8350 / −3938 |
| `-Os` on `common/` objects only | 201826 | 11166 / −1122 |
| `-Os` on `common/` + `telink/` objects | 200050 | 12942 / 654 |
| `-Os` everywhere incl. SDK | 195442 | 17550 / 5262 |

Flash attribution: ~101 KB precompiled Zigbee stack and libc (fixed), ~60 KB
SDK sources, ~40 KB own code. Largest own objects: `feature_wiring.o` 5.1 KB,
`telink/hal/zigbee_zcl.o` 5.1 KB, `cover_controller.o` 3.4 KB,
`switch_cluster.o` 2.6 KB, `config_parser.o` 2.1 KB, `button_event_cluster.o`
2.0 KB, `cover_switch_cluster.o` 2.0 KB. Cover code (controller + cover cluster
+ cover-switch cluster) is ~6.7 KB. `meter_service` contributes 16 bytes
(removed by `--gc-sections`); UART printf is disabled in release builds.

## 10. Socket discovery workstream (GPIO probe)

### Why

The BSEED outlet `TS011F` / `_TZ3210_drzlxjne` (Z2M `TS011F_plug_1`, Telink
OUI, router) cannot be opened. Its relay, button, LED and meter pins and its
meter IC are unknown. Published `_TZ3210` plugs differ: some use BL0937 (pulse
outputs CF, CF1, SEL), others BL0942 (UART). Sibling and third-party pin maps
disagree (CF C0 / CF1 C2 / SEL C1 versus CF PC3 / CF1 PC2 / SEL PB4). Pins must
be discovered on the device itself with a probe running in our firmware.

### Constraints

- Conversion from stock is one-way: recovery or return to stock needs the SWS
  pad, which needs opening. The first converted outlet must be expendable.
- The probe image installs over stock Tuya firmware, so its OTA file must fit
  the 200704-byte bootloader-layout slot (H10) with margin.
- Stock identity of the owner's outlets (captured 2026-10-14): Basic
  `appVersion` 82, `stackVersion` 0, `hwVersion` 1, `dateCode` empty,
  `swBuildId` unsupported; OTA `manufacturerCode` 4417, `imageType` 54179,
  `fileVersion` 82. Image type 54179 is shared by 149 stock Tuya entries in
  `zigbee2mqtt/ota/index_router.json`; only the `manufacturerName` filter keeps
  them apart. The index generator must refuse any entry with a stock image type
  that lacks a `manufacturerName` filter, and the probe board's entry must be
  filtered to `_TZ3210_drzlxjne`.
- For other outlets, record the stock identity from Z2M before conversion:
  `genBasic` `appVersion`, `stackVersion`, `hwVersion`, `dateCode`,
  `swBuildId`, and from the OTA "check for update" log `manufacturerCode`,
  `imageType`, `fileVersion`. The probe board's `stock_manufacturer_id` and
  `stock_image_type` come from these values, never from a sibling.
- The outlet board is not isolated from mains. All probing is done over Zigbee
  with the outlet in its housing; no load is plugged in during output pulses.

### Scope

1. **Probe board.** A `device_db.yaml` entry for the outlet with
   `features: [probe]`, a unique firmware image type, and a config that claims
   no pins. At boot every GPIO stays in its reset state (input, no output
   enable). The image contains management, OTA and the probe only.
2. **`gpio_probe_service`** (`src/base_components/`), independent of ZCL and
   NVM, compiled only for boards with the `probe` feature:
   - **pin set:** a per-MCU table of probeable pins. Pins needed by the
     platform are excluded: TLSR8258 SWS (PA7), flash and crystal pins, and any
     pin owned by an active feature.
   - **snapshot:** read the level of every probeable pin without changing its
     mode.
   - **watch:** count edges on up to N selected pins for 1–300 s, reporting
     count, minimum and maximum interval and last level per pin. The interrupt
     handler only increments counters. A pin above a rate ceiling (e.g. 20 kHz)
     stops counting and is flagged. This identifies the button (press it),
     BL0937 CF1 (steady pulses with no load, from mains voltage), CF (rate
     rises with a load) and a silent BL0942.
   - **pull test:** for each selected pin, enable pull-up, read, enable
     pull-down, read, restore no-pull input. Classify each pin as
     *floating* (follows both pulls), *driven high*, *driven low* or *active*
     (toggling). The classification is kept for the session.
   - **pulse:** drive one pin to a requested level for 1–500 ms, then restore
     no-pull input. Allowed only on pins classified *floating* in the current
     session's pull test, one pin at a time, with an explicit arm command
     before each pulse. The cutoff runs from a timer and is re-checked from the
     main loop. A reset or reboot always returns pins to input, because nothing
     is persisted. Used for relay (audible click), LEDs (visible) and SEL
     (CF1 rate changes between voltage and current).
   - every operation can be stopped; stop and timeout restore no-pull input.
3. **Zigbee interface.** Probe commands and results on the Device Diagnostics
   cluster 0xFC11 with the fork manufacturer code (D3), defined in
   `helper_scripts/management_protocol.json` and generated into firmware and
   templates. Pin ids are portable (`port << 8 | index`, A = 0).
4. **Discovery helper.** A script that drives the probe through
   Zigbee2MQTT's MQTT API and runs the procedure: snapshot → pull test →
   watch with no load → watch while the user presses the button → watch with a
   resistive load → pulse each floating pin with the load unplugged, asking the
   user what they heard or saw. It prints a proposed config string and meter
   pin map. Document the procedure in `docs/`.
5. **Tests.** Stub models for floating, pulled and externally driven pins and a
   pulse-train source. Native unit tests for classification, the rate ceiling,
   pulse refusal on non-floating pins, cutoff and stop. Telink 4-gang and probe
   board builds, with size recorded.

### Out of scope for P

- BL0942 UART probing (send a read request on candidate pins). Add it only if
  the watch step shows no pulse activity.
- Meter driver, measurement clusters and calibration (frozen Phase 5/7).
- The output-cutoff guarantees of the original Phase 6.4 design beyond the
  timer-plus-main-loop cutoff above.

### Hardware procedure (owner)

1. Read and record the stock identity values listed above.
2. OTA the probe image onto one expendable outlet with nothing plugged in.
3. Run the discovery helper and record the results in
   `docs/refactor/capabilities/STATE.md`.
4. With pins and meter IC known, write the outlet's real profile and unfreeze
   the meter driver work.

## 11. Progress log

| Date | Wave | Integration commit | Evidence | Result |
| --- | --- | --- | --- | --- |
| 2026-10-14 | 0 | `f3782569` | S1, S7, AGENTS.md compatibility text, decisions; `make tests` 282 passed | done |
| 2026-10-14 | 1 F | `69e1929e` (ff) | C4 (version from file, migration test with mutation checks), H10 (OTA-file size check), H9 (Silabs compiles: ZS2S_TS0001 router, MOES_TS0011 end device); independent review PASS WITH NITS, nits fixed | merged |
| 2026-10-14 | 1 B | `fix/wave1-b-input-timing` merge | S5, S6, C7, C8, H2, H3, H4, M11, M12; review PASS WITH NITS, nits fixed (sleep-safe tick refresh, backward-tick guard, test gaps, `D<N>` 1–200) | merged |
| 2026-10-14 | — | `45a3bab4` | debounce default 25 ms (owner decision) | done |
| 2026-10-14 | 1 A | `220cc6e4` | S2–S4, C1, C2, C3 (ZCL_CLUSTER_NUM_MAX 48), H1, H5, H8, M8, M9, frame check before every SDK foundation parser incl. 0x08, `zcl_init` hook leak fix, recovery drives relays off; review + security re-review (5M random frames under ASan) passed after fixes; integrator mutation check of the 0x08 guard; `make tests` 333 passed, unit tests pass, 0 warnings; sk8 4-gang build OTA file 204754 bytes (−4050 vs 200704) | merged; wave 1 complete |

Open after wave 1: Write Undivided validates records against current state
(documented limitation in `src/hal/zigbee.h`); `ZCL_REPORTING_TABLE_NUM` 12 is
exactly full on the 4-gang with Z2M (raising it needs an NV migration of
`NV_ITEM_ZCL_REPORT`); `timer_service` arm failures have no retry
(`relay_driver.c` latching pulse, `cover_controller.c` safety timer); stub
`step_time` fires due tasks only at step end; intermittent pytest thread warning
from `tests/client.py` `_stderr_forward`; Silabs `-Wformat` warnings for
`uint32_t`; cover-switch long-press duration is not persisted
(`cover_switch_cluster.c:389`); end-device `_end_bss_` exceeds the 0x848000
retention limit (pre-existing).
| 2026-10-14 | 1.5 S | `961d28ee` | D6 per-board `features` (schema, `make tools/check_device_features` in CI, `FEATURE_*` defines, parser rejects missing-feature tokens → recovery LastError 8 `FEATURE_MISSING`), D7 `-Os` on own Telink objects; review PASS WITH NITS, nits fixed by integrator (`820d12fb`); `make tests` 343 passed, unit tests pass, 0 warnings; 4-gang OTA 192114 (8590 under 200704), GIRIER TS130F cover 199266 (1438), MHCOZY end device 161618 | merged |
| 2026-10-14 | 2 D | `fix/wave2-d-cover` merge | H6, L5, cover cutoff-timer retry (stop + `ACTION_DENIED` when unarmable), cover-switch long press persisted (`NVM_MIGRATIONS_VERSION` 10, converting step), M6 confirmed by tests; H7 firmware keeps ZCL lift % (0 = open), unknown = 0xFF (Z2M drops values > 100); review PASS WITH NITS, nits fixed (`7b2254e6`) | merged |
| 2026-10-14 | 2 C | `4da53063` | C9 (config `K` authoritative, writes limited to config groups, relay records reset when endpoint→relay map or K membership changes; fingerprint NVM item 101), M1, M3, M4, M5 (inching in `relay_controller` for every ON source, timed-on capped by inching), L7 (`K` default dead time 200 ms), latching coil retry, joining a group while a peer is on switches the joiner off; review PASS WITH NITS, nits + size cuts fixed; `make tests` 370 passed, unit 0 failures, 0 warnings; sk8 OTA: GIRIER TS130F 1G 200146 / 2G 200162 (≈550 under 200704), 4-gang 192690 | merged |
| 2026-10-14 | 2 P | `feat/wave2-p-probe` @ `05c97753` | probe service, TLSR8258 HAL, 0xFC11 probe commands, board `OUTLET_BSEED_TS011F_PROBE` (image type 47116, `build: no`), index guard for stock image types, discovery helper; pulse safety mutation-checked by implementer and reviewer; probe OTA 190866 (9838 under 200704); review: firmware safe, deliverable FAIL (section 12 step 2) | pushed, unmerged |
| 2026-10-15 | fix pins | `c9706ef5` | blocker from section 12 step 1: parser takes pin ids from `hal_gpio_parse_pin` (Telink `port<<8 \| 1<<n`, Silabs `port<<8 \| n`, invalid `0xFFFF`), private `<<4` encoding deleted; stub uses the Telink-like encoding (163 of 370 tests failed against the old parser); SDK-free pin headers with compile-time checks against the SDKs and host unit tests; Telink `size_t` clash fixed (`27e20e35`); review: merge, mutation-checked; `make tests` 370 passed; sk8 OTA: GIRIER 1G 200306 / 2G 200322 (≈400 under 200704), 4-gang 192866; Silabs ZS2S_TS0001 and MOES_TS0011 build (known `-Wformat`/`noreturn` warnings only), closing the wave 2 C/D Silabs debt | merged |
| 2026-10-15 | 2 P | `d95120b1` | follow-ups: `check-publishable` refuses probe boards in `board/build`/`update-indexes` (fails closed) and the index generator refuses probe entries; discovery helper reads one attribute per request; `paho-mqtt` in `requirements.txt`; stub transistor-base relay model, 10 kΩ pull-up kept (1 MΩ makes every transistor-driven relay pin read driven low; mutation-checked); docs (main-loop pulse cutoff, leaving the probe image); claimed pins converted with `hal_gpio_probe_id_from_pin`; review fixes by integrator; `make tests` 394 passed; probe OTA 191314, non-probe sizes unchanged; no index holds a probe entry | merged |
| 2026-10-15 | 2b E | `8cc252f0` | C5, C6, D3: Z2M/ZHA/HOMEd use 0xFC10/0xFC11 and manufacturer code `0x1A2B` on every fork-private cluster, generated from `management_protocol.json` (`0x1A2B` unassigned in zigbee-herdsman 10.9.3); `coverInverted` removed, cover settings 0xFF02–0xFF06 and cover-switch long press exposed; ZHA clears lift 0xFF; ZHA multistate present value read-only; Z2M probe converter `TS011F-probe`; `tests/test_integration_parity.py` compares every generated attribute with the stub's registered attributes (mutation-checked); outputs regenerated separately; no firmware change; review: merge; `make tests` 401 passed; offline checks only (herdsman-converters 26.108.1, zha-quirks 2.3.0) | merged |
| 2026-10-15 | release | `4dfac79b`, tag `v2.0.0-rc.1` (`bbe7976b`) | fork versioned as 2.0.0 (`helper_scripts/firmware_version.py`: file version `MAJOR<<4\|MINOR, PATCH, rc or 0xFF, BUILD_NUMBER`, out-of-range parts fail the build; replaces the git-count byte that wrapped at 256 and depended on `origin/main`); TS0726 4-gang `2.0.0-rc.1-a4663` built on sk8 from a clean clone, 192882 B, file version 0x20000100; index: one normal entry for `_TZ3002_pzao9ls1` plus FORCE, URL pinned to the image commit (H0.1), no `from_tuya` entry; all `538f75d3` entries and images removed; downloaded image matches the index sha512 | published, not flashed |
| 2026-10-15 | rc.1 failure | `db4f5ced` | rc.1 on the owner's 4-gang reset on every relay change and on most incoming ZCL foundation commands (Write Attributes, Configure Reporting, Default Response; Read Attributes unaffected), looping when Z2M replayed failed writes. Cause: tc32-gcc 4.5.1 `-Os` switch tables (byte-relative entries dispatched as 32-bit absolute addresses) in `hal_zigbee_telink_foundation_frame_ok`, `switch_cluster_binding_action_on/off`, `action_mapper_dispatch`, `bdb_commissioning_callback`; invisible to the host stub. Confirmed by disassembly and TC32 emulation (`.claude/worktrees/rc1-relay-investigation/experiments`). Fix: `-fno-jump-tables` for all Telink objects plus a post-link guard (mutation-checked: removing the flag fails the link naming the 5 functions); review: merge. Sizes: 4-gang 192818, GIRIER 1G 200274 | merged; device on rc.1 pending OTA recovery |
| 2026-10-15 | rc.2 release | `9577cb03`, images `c87b90e3` | TS0726 4-gang `2.0.0-rc.2-68a06` (192818 B, 0x20000200, jump-table guard passed) OTA-installed over rc.1 on the owner's switch using a temporary Z2M recovery definition (OTA only, no configure/writes) after a Z2M restart cleared replayed writes; then regenerated converter: interview, configure (Configure Reporting) succeeded; owner smoke test: local presses, double/triple clicks, relay toggles (146 events), detached mode and LED setting writes; 0 reboots, 0 errors | first build verified on hardware |
| 2026-10-15 | 3 firmware | `bfd39b31` | Telink: clusters without a command handler answer UNSUP_(MANU_)CLUSTER_COMMAND instead of calling NULL (SDK patch + sdk.mk check); M2 switch drives its relay endpoint's relay; M13 periodic reports carry the cluster manufacturer code (SDK patch); L10 private cluster registration by tracking; L3 `i<image_type>` works on Telink; M7 diagnostics refresh 1 s (uptime not reportable); Telink E4–E7 rejected; jump-table guard fails on any untraced indirect jump. Review: merge, mutation-checked. `make tests` 432; GIRIER 200434 (≈250 B headroom) | merged, not on hardware |
| 2026-10-15 | port E | `7d5987f9` | Telink configs reject all port E pins (PE0–PE3 are the in-package flash SPI) | merged |
| 2026-10-15 | probe install | — | probe image `2.0.0-rc.2-ad164` (built from ad1641f1, guard passed, 191282 B, from_tuya 4417/54179/0xFFFFFFFF, sha256 f3e1eb3a…a93a81) emulated in TC32 emulator (all 0xFC11 commands OK, refusals OK, pins restored); OTA started on outlet "Salon Gniazdko lewe RTV" 0xa4c138ecd57ae350 via single-device Z2M update with URL served from sk8 (no index entry) | in progress |

## 12. Next steps (current as of the wave 2 P merge)

Waves 0, 1, 1.5 and 2 (C, D, P) and the pin-encoding fix are merged into
`refactor/input-pipeline`. Nothing has been flashed. The OTA index still lists
the old `538f75d3` TS0726 build; the owner does not install it.

### Process rules (learned during waves 0–2)

- Create each worktree by hand from the integration branch
  (`git worktree add .claude/worktrees/<name> -b <branch> refactor/input-pipeline`)
  and point the agent at that path. The Agent tool's `isolation: worktree`
  bases worktrees on upstream `main` (`4ca68fa2`), not on this branch.
- One implementing agent per slice with a short self-contained brief; an
  independent reviewer per branch before merge; the integrator mutation-checks
  security-relevant fixes before merging.
- For review follow-ups, launch a fresh agent with only the remaining items,
  pointed at the existing worktree; do not resume an agent whose context is
  already large.
- Telink and Silabs builds run on `sk8` (section 9), one remote directory per
  worktree (`~/Projects/tz-<name>`, with a `telink_tools` symlink to
  `~/Projects/tuya-zigbee-switch/telink_tools`). Silabs tools live in
  `~/Projects/tz-wave1-f/silabs_tools`, environment `~/Projects/tz-wave1-f-env.sh`.
- Push only to the fork remote `desty2k`, never to `origin` (upstream).
- The owner's usage budget is limited: keep briefs short, avoid extra agents.

### 1. Done: pin encoding (section 11, 2026-10-15)

Remaining nits for wave 3: Telink accepts E4..E7 (TLSR8258 SDK defines
PE0..PE3) and Silabs accepts every index 0..15 on ports A..D, more than MG21
has; `src/telink/hal/gpio.c` comment says 4 ports (there are 5); the parser's
`> 15` check duplicates the HAL's.

### 2. Done: wave 2 P follow-ups (section 11, 2026-10-15)

Open: the probe Z2M converter (wave 2b E); Telink cluster-specific commands to
0xFC10/0xFC11, on/off-config and multistate call a NULL handler in non-probe
images (fixed for probe only; 68 B, GIRIER headroom now ≈400 B); helper
`probe_discovery.py` can see a stale `probe.<name>` merged from Z2M's cached
state and prints a raw traceback when the broker is unreachable. Owner
decision: the probe never pulses driven-low pins (relays behind a base
resistor ≤ 4.7 kΩ are found by elimination, not by pulsing).

### 3. Verification debt

- GIRIER TS130F headroom is ≈400 B; any further cover/relay code needs size
  cuts first.

### 4. Done: wave 2b E (section 11, 2026-10-15)

Open: no live test on Z2M, ZHA or HOMEd; ZHA quirk emits a cleared and an
updated event for lift 0xFF (possible duplicate, check on live ZHA); HOMEd
shows lift 0xFF as an out-of-range position; button-event command carries no
manufacturer code (both integrations decode it). Renamed keys for existing
users are listed in `docs/changelog_fw.md`.

### 5. Wave 3 — cleanup

L2, L3 (`i<image_type>` token ignored), L4 (`feature_wiring.c` readability),
L8, L9, L10, M2, M7, M10, M13, and: stub `step_time` fires due tasks only at
step end; intermittent pytest `_stderr_forward` warning; Silabs `-Wformat`
warnings; end-device `_end_bss_` beyond the 0x848000 retention limit;
`ZCL_REPORTING_TABLE_NUM` decision (needs an `NV_ITEM_ZCL_REPORT`
migration to grow); Write Undivided validates against current state. L1 is
resolved by D6. Owner decision pending: the relay InterlockGroup attribute is
effectively read-only without a `K` token — keep (recommended) or remove.

### 6. Release and hardware

1. Done: `v2.0.0-rc.1` for the TS0726 4-gang (section 11). Further releases
   follow `docs/contribute/release.md`. Version strings keep only 5 hash
   characters (16-character SWBuildID); file names use the same string.
2. Owner hardware pass (section 8): the custom-firmware 4-gang first, then one
   spare stock BSEED switch over the stock slot; record results in
   `docs/refactor/*/STATE.md`.
3. Outlet: OTA the probe image to one expendable outlet with no load, run the
   discovery helper, record pins and meter IC, then write the outlet profile and
   unfreeze the meter driver (Phase 5/7).
