# 04 — Zigbee write safety and input polarity

Phase 1, priority 2 in implementation order. This ports the narrow correctness
parts of upstream PR 405.

## Safe endpoint dispatch

Attribute writes contain network-controlled endpoint and cluster ids. Dispatch
must locate a registered object and return harmlessly when none exists.

Replace direct expressions such as:

```c
switch_cluster_by_endpoint[endpoint]
```

with checked lookup helpers:

```c
zigbee_switch_cluster *switch_cluster_find(uint8_t endpoint);
zigbee_relay_cluster *relay_cluster_find(uint8_t endpoint);
zigbee_cover_cluster *cover_cluster_find(uint8_t endpoint);
zigbee_cover_switch_cluster *cover_switch_cluster_find(uint8_t endpoint);
```

Each helper validates the endpoint domain before indexing and may return `NULL`.
Every attribute-write trampoline returns without side effects on `NULL`. Command
trampolines return `HAL_ZIGBEE_CMD_SKIPPED` for a missing object. Cluster-domain
functions may assert or reject `NULL`; the trampoline boundary owns untrusted
lookup validation.

This applies to switch, relay, cover and cover-switch paths. The button-event
cluster already searches a bounded list and keeps its NULL guard.

## Pull-derived polarity

One helper defines the electrical default for every parsed input token:

```c
static bool button_active_high_from_pull(hal_gpio_pull_t pull) {
    return pull == HAL_GPIO_PULL_DOWN;
}
```

| Token | Pull-up / pull-up 1 MΩ | Pull-down | Floating / invalid |
| --- | --- | --- | --- |
| `B` on-board button | active low | active high | configuration error |
| `S` switch | active low | active high | configuration error |
| both `X` cover inputs | active low | active high | configuration error |

The parser stores the pull-derived active level in `button_config_t`; it does not
inspect the raw token character separately in three branches.

## Runtime mode interaction

Electrical polarity and switch semantics are separate values. A momentary-NC
mode may explicitly invert the logical pressed condition, but applying a mode
must be expressed relative to the parsed electrical baseline rather than
overwriting it with an enum comparison.

`button_config_t` therefore retains either:

- `electrical_active_high` plus a runtime semantic inversion flag; or
- a derived `active_high` recomputed by one helper from pull and mode.

Loading NVM and writing the mode attribute use the same helper. Re-seeding the
button FSM after a mode change emits no synthetic event.

For the target BSEED configuration, every `S...u` input is electrically active
low and the final `M` token selects momentary semantics without changing that
electrical fact.

## Parser validation

Input count is checked before writing `button_configs`, gesture configs, role
tables or endpoint objects. An invalid pull or exhausted input slot aborts
configuration construction through the common fail-closed path described in
[05_resource_bounds.md](./05_resource_bounds.md).

## Tests

- Attribute writes to endpoint `0`, the maximum valid id, maximum+1 and `255`
  cause no crash or mutation when no matching cluster exists.
- Command trampolines return `HAL_ZIGBEE_CMD_SKIPPED` for missing endpoints.
- `B`, `S` and both `X` inputs run a pull-up/pull-down press and release matrix.
- Mode is loaded from NVM and written at runtime for both pull directions.
- A polarity/mode change while the input is released reseeds state with no
  `DOWN`, `UP`, relay, binding or `0xFC02` output.
- The exact BSEED config yields four released buttons when all four GPIOs are
  high at initialization.

Reference: [upstream PR 405](https://github.com/romasku/tuya-zigbee-switch/pull/405).

