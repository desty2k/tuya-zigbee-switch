# Known issues

Limits of the current 2.x firmware. Report new problems on the fork's
[issue tracker](https://github.com/desty2k/tuya-zigbee-switch/issues).

## Release status

- Every board except the Sonoff ZBMINIL2 (below) is published as a release
  candidate (2.0.0-rc.4). Only the boards listed as tested in the
  [readme](../readme.md#devices) ran 2.x on hardware.
- Silabs (EFR32MG21/MG22) builds are not hardware-tested in 2.x. Silabs devices
  cannot be converted from stock firmware over the air, and the power meter
  (`E` token) is Telink only.
- Hardware testing of 2.x has been short: no board has run a multi-day test,
  a series of power cuts or long coordinator outages yet
  ([open_work.md](./contribute/open_work.md#hardware-tests-not-run)).
- `MODULE_SONOFF_ZBMINIL2` (EFR32MG22) does not build in 2.x (its RAM
  overflows) and has no 2.x image; it is not part of the release.

## Child lock state after a missed report

No attribute reporting is configured for `child_lock`, because the Telink
attribute reporting table has no slot left on 4-gang boards. A lock toggled on
the device sends one report to the coordinator; a coordinator that misses it
shows the old state until it reads the attribute again (Zigbee2MQTT: refresh
or `get`).

## Cover position while unknown

While a cover's position is unknown (`0xFF`, see
[configuration.md](./usage/configuration.md#how-a-cover-moves)), Zigbee2MQTT
keeps showing the last position and ZHA shows none, but HOMEd cannot filter the
value and shows an out-of-range position until the next completed move to an
end.

## Power meter accuracy

The BL0937 scale of `OUTLET_BSEED_TS011F_PM_DRZLXJNE` is not yet checked
against a reference meter; readings may be off by several percent. Calibrate
with a reference meter ([configuration.md](./usage/configuration.md#meter-e-token-telink-only)).

## Two-button gestures on touch panels

Touch panels whose touch chip reports one pad at a time cannot press two
buttons together: the two-button child lock gesture and stopping a cover with
open and close together do not work on them. Covers with a stop button use a
5 s hold of stop for the child lock instead.

## Integration differences

- HOMEd receives no [button events](./usage/button_events.md) and does not
  offer the `deferred_single` relay mode.
- ZHA and HOMEd do not expose the [config string](./usage/config_string.md);
  use Zigbee2MQTT or a raw attribute write.
- The integrations offer `multi_press_reset_count` up to 255; the device
  accepts 0–10 and refuses larger values.

## End devices and group commands

End-device firmware does not receive Zigbee group commands, as on the stock
firmware: group commands reach only devices that keep their receiver on. Use
router firmware or a Home Assistant group for devices in groups.

## Power draw of no-neutral switches

The firmware draws more current than the stock firmware, so a no-neutral
(L-only) switch needs a larger load: estimated more than 4 W as an end device
and 8 W as a router. A smart bulb (under 1 W when off) is not enough without a
bypass capacitor.
