# Flashing Silabs devices by wire

How to flash a Silabs (EFR32MG21) device over SWD with a J-Link: the only way
to convert a Silabs device from its stock firmware. The Sonoff ZBMINIL2 has
its own guide: [zbminil2.md](./zbminil2.md).

> [!NOTE]
> Silabs images of the 2.x firmware are not hardware-tested yet ([known_issues.md](../known_issues.md#release-status)).

### What You'll Need

- A J-Link programmer (JLink v9 confirmed to work)
- A soldering iron
- Some spare jumper wires

### Step 1: Disassemble the Device

Unplug the device from the mains. **Never attempt to disassemble it while it is plugged in!**

In general, you need to disassemble devices until you have access to the ZS3L module or MCU itself. Here is a detailed example guide for the Moes 4 Gang switch.

First, pop the front glass panel:

![Front glass disassembly](/docs/.images/moes_4_gang_disassemble_front.jpg)

Then gently detach the PCB from the device. Alternate the sides you use for leverage to avoid damage to the connector.

![PCB disassembly](/docs/.images/moes_4_gang_disassemble_pcb.jpg)

### Step 2: Solder the Wires

You'll need to solder five wires as shown in the photos: GND, 3.3V, SWDIO, SWCLK, RST.

If you have a ZS3L module, you can use the following [Tuya docs](https://developer.tuya.com/en/docs/iot/zs3l?id=K97r37j19f496) for reference. Or just follow this schema:

![PCB connections](/docs/.images/meos_4_gang_pcb.jpg)

For Moes 4 gang, we can re-use the connector to plug in power:

![PCB power](/docs/.images/moes_4_gang_pcb_power.jpg)

But you have to solder to the other pins. There is no need for a long-term connection, but be sure not to bridge the pins together.

![PCB power](/docs/.images/moes_4_gang_pcb_wires.jpg)

Finally, attach the wires to your J-Link programmer (for JLINK V9, the connection should look like this, following the color schema used above):

![PCB power](/docs/.images/jlink_wires.jpg)

### Step 3: Flash the Firmware

Download both the bootloader and the firmware from [bin/](https://github.com/desty2k/tuya-zigbee-switch/tree/refactor/input-pipeline/bin): the generic bootloader `bin/efr32mg21_bootloader_generic.s37` and your board's image `bin/router/<BOARD>/tlc_switch-<version>.s37` (or `bin/end_device/<BOARD>_END_DEVICE/` for end-device firmware).

#### Using Commander Software

There are different software options that you can use for flashing, but one of the easiest options is to use Simplicity Commander from Silicon Labs. Download Commander from [this page](https://www.silabs.com/software-and-tools/simplicity-studio/simplicity-commander?tab=getting-started).

Plug the J-Link into your PC, then open Commander and select your flasher. It may be named as some random set of numbers, that's ok.

![commander select device](/docs/.images/commander_select_kit.png)

**Check the connection:**

Open the device info page to verify that the connection is good:

![commander device info](/docs/.images/commander_device_info.png)

If this doesn't work, you can try constantly tapping on the device touch button. This is because the device can be in deep sleep and the flasher cannot communicate with it. If this is your case, keep tapping until you erase the chip.

**Erase the chip:**

Open the flash tab and erase the chip. **THIS WILL DELETE STOCK FIRMWARE!** If you want to keep it, press "Open shell" below and use this command to backup the stock firmware:

```
commander readmem --device EFR32MG21 --region @mainflash --outfile stock_dump.bin
```

![commander erase](/docs/.images/commander_erase.png)

**Flash the bootloader and firmware:**

Now you'll need to flash both the bootloader and firmware separately.

1. Press "Browse":

   ![commander ](/docs/.images/commander_browse.png)

2. Select the bootloader file (`efr32mg21_bootloader_generic.s37`):

   ![commander ](/docs/.images/commander_select_bootloader.png)

3. Press "Flash" and wait until the process finishes:

   ![commander ](/docs/.images/commander_flash.png)

4. Now select your device-specific firmware file and press "Flash" again. Wait until the process finishes:

   ![commander ](/docs/.images/commander_select_firmware.png)

If done correctly, the onboard LED will start flashing automatically. You can try joining the device to a Zigbee network to verify that it connects properly. Once confirmed, disconnect everything, unsolder the wires, and reassemble the case.
