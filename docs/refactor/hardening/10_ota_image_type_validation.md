# 10 — OTA image-type validation

Phase 2, priority 4. This adopts the CI guard from the concept in upstream PR 474
without importing the fleet-wide renumbering and migration set.

## Invariant

Every buildable device entry with a non-null `firmware_image_type` has a unique
value in `device_db.yaml`, unless a future schema explicitly models a shared
binary identity. The current schema describes per-device image types, so an
unmodeled duplicate is an error.

The target BSEED entry keeps image type `43627`. It is unique in the current
database and requires no migration image.

## Validator

Add `helper_scripts/check_image_types.py` with bounded, machine-readable output:

```text
duplicate firmware_image_type 45578:
  SWITCH_BSEED_TOUCH_TS0002
  MODULE_TUYA_NOVATO_QS_S05_TS0011
```

Rules:

- load YAML through the project's normal Python dependency;
- validate integer range `0..65535`;
- ignore only explicit null values;
- report every key participating in each collision;
- sort output by image type and device key for deterministic CI;
- exit nonzero on invalid type or collision.

Because the database contains known collisions, rollout has two modes:

1. CI on a change compares the base and proposed database and rejects any newly
   introduced collision or any expansion of an existing collision set.
2. A full-database audit target reports all existing collisions and becomes
   enforcing only after a separately approved cleanup/migration.

The branch must not pretend the existing database is globally clean if it is
not. The narrow gate is that this hardening work does not worsen it and the
BSEED identity remains unambiguous.

## Integration

- `make tools/check_image_types` runs the validator.
- Pull-request CI invokes comparison mode with the merge base.
- Board build validates the selected entry's type before OTA packaging.
- Tests use small YAML fixtures; they never load or print the full generated
  device database into test logs.

## Renumbering policy

Changing an existing image type is an OTA compatibility change. It requires:

- an explicit old-to-new mapping;
- migration OTA artifacts carrying the old header id;
- manufacturer/model matching analysis for both ZHA and Zigbee2MQTT;
- index-generation tests;
- hardware OTA verification from a released image.

None of those changes are part of this phase.

## Acceptance

- A unique fixture passes.
- A duplicate, null, negative, value above `65535` and non-integer each produce a
  deterministic result.
- Comparison mode permits untouched known collisions and rejects a new member.
- The BSEED entry resolves to `43627` exactly once.
- No device id and no generated OTA artifact changes in this stage.

Reference: [upstream PR 474](https://github.com/romasku/tuya-zigbee-switch/pull/474).

