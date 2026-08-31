# 05 — Endpoint, cluster and attribute resource bounds

Phase 1, priority 3 in implementation order. This ports the generic hardening
identified by upstream PR 477 and applies it to Telink as a release requirement.

## Resource domains

The firmware constructs descriptors in shared arrays and then copies them into
platform-fixed tables. Every domain needs an explicit capacity:

| Domain | Current Telink/shared capacity | Target rule |
| --- | --- | --- |
| endpoints | shared `endpoints[10]`; Telink `MAX_ENDPOINTS = 8` | validate count before construction; BSEED count 8 is valid |
| endpoint-id dispatch | several `[10]` pointer tables | size by `HAL_ZIGBEE_ENDPOINT_ID_MAX + 1`; validate before indexing |
| shared clusters | `clusters[48]` | checked append for every cluster |
| Telink input clusters | `MAX_IN_CLUSTERS = 32` | aggregate preflight before pointer writes |
| Telink output clusters | `MAX_OUT_CLUSTERS = 32` | aggregate preflight before pointer writes |
| Telink attributes | `MAX_ATTRS = 128` | aggregate preflight before pointer writes |
| Telink cluster info | input + output capacity | validate aggregate cluster count |

Array length and valid endpoint id are different concepts. Endpoint 10 requires
an 11-entry direct-index table even when only ten endpoint objects exist.

## Shared builder API

Cluster modules stop writing `endpoint->clusters[endpoint->cluster_count]`
directly. A checked builder owns the append:

```c
bool hal_zigbee_endpoint_add_cluster(
    hal_zigbee_endpoint *endpoint,
    uint8_t endpoint_cluster_capacity,
    const hal_zigbee_cluster *cluster);
```

The device-config builder owns a cursor and remaining shared-cluster capacity.
Multi-cluster additions such as switch (five entries) and relay plus Groups
(three entries) reserve the full amount before writing the first entry. A failed
reservation cannot leave a partially constructed endpoint.

Attribute arrays embedded in cluster structs use compile-time capacity checks
where their count is fixed. Dynamic attribute counts validate the selected
count before registration.

## Platform preflight

`telink_zigbee_hal_zcl_init()` performs a read-only preflight over the complete
descriptor graph:

1. endpoint pointer/count consistency;
2. endpoint count and id domain;
3. cluster pointer/count consistency per endpoint;
4. input/output cluster totals;
5. attribute pointer/count consistency per cluster;
6. aggregate attribute total;
7. duplicate endpoint ids;
8. required registration function availability for server clusters.

Only after preflight succeeds may it write `endpoint_descriptors`,
`in_clusters`, `out_clusters`, `cluster_infos` or `attr_tables`. Failure leaves
the Zigbee stack uninitialized, records a diagnostic reason and enters the
existing recoverable configuration-error/reset path. Silent truncation is not
allowed.

The Silabs HAL adopts the same all-or-nothing preflight for its 48-cluster and
128-attribute buffers, but Silabs networking changes do not enter this branch.

## Target-board budget

`SWITCH_BSEED_TS0726_4GANG` expands to four switch and four relay endpoints:

```text
endpoint count = 4 + 4 = 8 = Telink MAX_ENDPOINTS
endpoint ids   = 1..8
```

Tests calculate actual input clusters, output clusters and attributes from the
constructed graph and assert they fit their Telink capacities. The expected
totals live in the test output or are derived by the validator; they are not
duplicated as hand-maintained production constants.

## Compile-time protections

- Define one capacity constant beside each storage array.
- Use `_Static_assert` for direct-index array sizes and fixed cluster attribute
  counts.
- Avoid magic `[10]` endpoint tables.
- Use `size_t` or a sufficiently wide unsigned accumulator during preflight so
  summation cannot wrap before comparison.

## Diagnostics

Expose a count of descriptor-validation failures and a compact last-failure enum:
`ENDPOINT_COUNT`, `ENDPOINT_ID`, `DUPLICATE_ENDPOINT`, `CLUSTER_CAPACITY`,
`ATTRIBUTE_CAPACITY`, `NULL_POINTER`, or `UNREGISTERED_CLUSTER`.

## Acceptance

- Exact capacity succeeds; capacity+1 fails before any platform buffer changes.
- Endpoint id at the maximum valid value is safe; maximum+1 and `255` fail.
- Invalid descriptor fuzzing under ASan/UBSan reports no memory error.
- The BSEED router stub graph and Telink build pass preflight.
- No direct unchecked `clusters[cluster_count]` append or unvalidated
  `*_by_endpoint[endpoint]` lookup remains.

Reference: [upstream PR 477](https://github.com/romasku/tuya-zigbee-switch/pull/477).

