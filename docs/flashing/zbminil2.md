# Flashing the Sonoff ZBMINIL2 by wire

How to open the Sonoff ZBMINIL2 (EFR32MG22) and flash it over SWD with a
J-Link.

> [!WARNING]
> **No 2.x image exists for the ZBMINIL2 yet.** The board
> (`MODULE_SONOFF_ZBMINIL2`) does not build in 2.x (its RAM overflows) and is
> not part of the 2.0.0-rc.5 release
> ([known_issues.md](../known_issues.md#release-status)). The files under
> `bin/router/MODULE_SONOFF_ZBMINIL2/` and
> `bin/end_device/MODULE_SONOFF_ZBMINIL2_END_DEVICE/` are 1.x images; they do
> not match the fork's converters. Do not flash them.

**Flashing custom firmware on ZBMINIL2 is irreversible**

Device uses cryptographic protection and so stock firmware cannot be dumped and restored. Proceed with caution.

### What You'll Need

- A J-Link programmer (JLink v9 confirmed to work)
- A soldering iron
- Some spare jumper wires

Optional, but preffered if you plan to flash a lot of devices:
- [3d-print pogopin holder](https://www.thingiverse.com/thing:7235444)



### Step 1: Disassemble the Device


Unplug the device from the mains. **Never attempt to disassemble it while it is plugged in!**

Pop-open the device body:

![ZBMINIL2 disassembly](/docs/.images/zbminil2_open.jpg)

You should be able to extract the device itself:


![ZBMINIL2 inside](/docs/.images/zbminil2_inside.jpg)

### Step 2: Attach pogopin holder

Attach ground to L-in terminal, and that attach pogopin holder as on photo:


![ZBMINIL2 with pogo](/docs/.images/zbminil2_with_pogo.jpg)

Make sure that you line up it correctly, two pins should touch TCK and TMS pads.

#### Solder wires (if you do not use pogoping holder)

You'll need to solder 3 wires, to the pads as shown on photo:

![ZBMINIL2 side](/docs/.images/zbminil2_side.jpg)

![ZBMINIL2 bottom](/docs/.images/zbminil2_bottom.jpg)

#### Connect wires to J-Link programmer:

Finally, attach the wires to your J-Link programmer.
Connect:
- L-in to ground
- 3.3v device pin to 3.3v source
- TCK to SWCLK
- TMS to SWIO 


### Step 3: Flash the Firmware

You need the bootloader `bin/efr32mg22_bootloader_module_sonoff_zbminil2.s37`
from [bin/](https://github.com/desty2k/tuya-zigbee-switch/tree/refactor/input-pipeline/bin)
and a 2.x `.s37` image of the board, which is not available yet (see the
warning above).

#### Using Commander Software

There are different software options that you can use for flashing, but one of the easiest options is to use Simplicity Commander from Silicon Labs. Download Commander from [this page](https://www.silabs.com/software-and-tools/simplicity-studio/simplicity-commander?tab=getting-started).

Plug the J-Link into your PC, then open Commander and select your flasher. It may be named as some random set of numbers, that's ok.

![commander select device](/docs/.images/commander_select_kit.png)

**Check the connection:**

Open the device info page to verify that the connection is good:

![commander device info](/docs/.images/commander_device_info.png)

**Unlock the chip:**

For some reason, GUI commander doesn't work well for me on Linux, so I was able to unlock via CLI only:
```
commander device --device efr32mg22 unlock
```

But in general, you should be able to click "Remove protection" button on "Flash" tab.

![commander erase](/docs/.images/commander_erase.png)

**Flash the bootloader and firmware:**

Now you'll need to flash both the bootloader and firmware separately.

1. Press "Browse":

   ![commander ](/docs/.images/commander_browse.png)

2. Select the bootloader file (`efr32mg22_bootloader_module_sonoff_zbminil2.s37`).  
  Screenshot is just illustration, please use zbminil2 file:

   ![commander ](/docs/.images/commander_select_bootloader.png)

3. Press "Flash" and wait until the process finishes:

   ![commander ](/docs/.images/commander_flash.png)

4. Now select the firmware file (the board's 2.x `.s37` image) and press "Flash" again.  
  Screenshot is just illustration, please use zbminil2 file. Wait until the process finishes:

   ![commander ](/docs/.images/commander_select_firmware.png)

If done correctly, the onboard LED will start flashing automatically. You can try joining the device to a Zigbee network to verify that it connects properly. Once confirmed, disconnect everything, unsolder the wires, and reassemble the case.


