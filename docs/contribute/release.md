# Release process

## Versions

`VERSION` holds `MAJOR.MINOR.PATCH` for a release or `MAJOR.MINOR.PATCH-rc.N`
for a release candidate. `helper_scripts/firmware_version.py` turns it into
the Basic cluster `SWBuildID` (`<version>-<git hash>`, cut to 16 characters)
and the 32-bit OTA file version that Zigbee2MQTT and ZHA compare as an integer:

| Byte | Content | Range |
| --- | --- | --- |
| 3 | `MAJOR << 4 \| MINOR` | 0–9 each |
| 2 | `PATCH` | 0–255 |
| 1 | rc number, `0xFF` for the final release | 1–254 |
| 0 | `BUILD_NUMBER`: 0 for published builds, 1–255 for development builds between two published ones | 0–255 |

`2.0.0-rc.1` is `0x20000100`, `2.0.0` is `0x2000FF00`; every candidate sorts
below its release and above the previous candidate. A value out of range stops
the build.

## Publishing a build

Start from a commit on which `make tools/check_device_features`,
`make unit_tests`, `make tests` and `make tools/check_management_protocol`
pass.

1. Set `VERSION`, move the `## Upcoming` changelog entries under the version's
   heading in `changelog_fw.md`, commit.
2. Build each board to publish from a clean checkout of that commit
   (`BOARD=<board> make board/build`), so the file names carry the commit hash.
3. Commit the images under `bin/` on their own.
4. Run `make tools/freeze_ota_links` so the index URLs name the image commit
   instead of the branch, review the index diff, and commit it separately.
   Only boards meant for this release may gain entries; keep `from_tuya`
   entries out unless stock devices are meant to be converted.
5. Before pushing the index, install the image on one device you can watch and
   check: a local button press toggles its relay, a Zigbee2MQTT setting write
   takes effect, and the device stays up for 10 minutes and after a rejoin.
   The host stub cannot catch compiler or SDK faults.
6. Tag the image commit `v<version>` and push the branch and the tag to the
   fork.
