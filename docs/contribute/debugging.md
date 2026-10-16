# Debugging

[3D_model]: https://www.printables.com/model/763948-tuya-cbu-flashing-jig

## Diagnostics without a probe

Most input and network problems show up in counters that need no wiring:

- **Device Diagnostics cluster** (`0xFC11`, endpoint 1, fork manufacturer
  code): uptime, GPIO edges captured and dropped, re-arm limit hits, button
  events and gestures, the button event queue (submitted, dropped, expired,
  send failures, high water), network transitions, losses, joins, announces,
  steering attempts and descriptor validation failures. The values refresh
  once per second. Read them from Zigbee2MQTT, ZHA or HOMEd through the
  generated integrations.
- **Device Config cluster** (`0xFC10`): `Status` and `LastError` tell why a
  config was rejected or why the device runs the recovery graph
  (`PARSE`, `RESOURCE`, `FEATURE_MISSING`, ...).
- **Host stub**: reproduce the behaviour with the same config string
  (`make stub/run`, or `./build/stub/stub_device --device-config "<string>"`),
  then use `diag` (the counters above plus stub-only ones), `input_diag`
  (task scheduling failures, dropped edges) and `zcl_graph_usage` (Zigbee
  resources against the platform limits). See [tests.md](./tests.md).

The counters are described in
[architecture.md](./architecture.md#diagnostics).

## Serial output

Debug builds print log messages (`printf`) on a UART:

```bash
BOARD=<key> DEBUG=1 make board/build-firmware
```

[`make_scripts/make_debug_single.sh`](../../make_scripts/make_debug_single.sh)
builds the router and end-device debug images of one board.

| Platform | Effect of `DEBUG=1` | TX pin, baud rate |
| --- | --- | --- |
| Telink | Defines `UART_PRINTF_MODE=1` (`src/telink/Makefile`); without it `printf` compiles away (`src/hal/printf_selector.h`) | `PB1` (`DEBUG_INFO_TX_PIN` in [`src/telink/configs/app_cfg.h`](../../src/telink/configs/app_cfg.h)), 115200 |
| Silabs | Keeps the `iostream_usart` component (`src/silabs/Makefile`); release builds replace it with `iostream_dummy` | `PA5` ([`src/silabs/config/sl_iostream_usart_inst_config.h`](../../src/silabs/config/sl_iostream_usart_inst_config.h)), 115200 |

The Telink TX pin `PB1` is often wired to a peripheral on the board; check the
[diagrams/](../diagrams/). In a debug image it carries the UART output.

You can power the device with 3.3 V DC from the programmer; it behaves the
same as on mains.

> [!WARNING]  
> Do not open the device while connected to mains!  
> Do not connect both mains voltage and 3.3V at the same time!  

Connect the TX pin of the device to the RX pin of a USB serial adapter
(SWS and RST are only needed for flashing). Keep the wires in place by
soldering, pogo pins, a [3D printed jig][3D_model], or a PCBite kit; some
components may need desoldering to reach the pins.

Find the port:

```bash
ls -l /dev/serial/by-id
```

Stop any flasher that holds the port, then start a serial monitor:

```bash
sudo minicom -b 115200 -o -D /dev/ttyUSB0
```
