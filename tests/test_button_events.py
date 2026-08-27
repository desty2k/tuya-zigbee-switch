import struct

from conftest import wait_for

from tests.zcl_consts import (
    ZCL_ATTR_BASIC_BUTTON_EVENTS_EMITTED,
    ZCL_ATTR_BASIC_GESTURES_EMITTED,
    ZCL_ATTR_BASIC_GPIO_EDGES_CAPTURED,
    ZCL_ATTR_BUTTON_EVENT_DEBOUNCE_MS,
    ZCL_ATTR_BUTTON_EVENT_LAST_EVENT_SEQ,
    ZCL_ATTR_BUTTON_EVENT_MULTI_CLICK_GAP,
    ZCL_ATTR_BUTTON_EVENT_STATE,
    ZCL_CLUSTER_BASIC,
    ZCL_CLUSTER_BUTTON_EVENT,
    ZCL_CMD_BUTTON_EVENT,
)

BUTTON_DOWN = 0
BUTTON_UP = 1
BUTTON_HOLD_START = 2
BUTTON_HOLD_END = 3
BUTTON_N_CLICK = 4


def decode_event(data: bytes) -> tuple[int, int, int, int, int]:
    return struct.unpack("<HHBBH", data)


def button_commands(device, endpoint: int = 1):
    return device.zcl_list_cmds(
        endpoint, ZCL_CLUSTER_BUTTON_EVENT, dst="coordinator"
    )


def wait_for_button_commands(device, count: int, endpoint: int = 1):
    wait_for(lambda: len(button_commands(device, endpoint)) >= count)
    return button_commands(device, endpoint)


def test_down_up_and_click_are_ordered_snapshots(device, button_pin):
    device.clear_events()
    device.press_button(button_pin)
    down = device.wait_for_cmd_send(
        1, ZCL_CLUSTER_BUTTON_EVENT, ZCL_CMD_BUTTON_EVENT, dst="coordinator"
    )
    assert decode_event(down.data) == (0, 1, BUTTON_DOWN, 0, 0)
    assert device.read_zigbee_attr(
        1, ZCL_CLUSTER_BUTTON_EVENT, ZCL_ATTR_BUTTON_EVENT_STATE
    ) == "1"

    device.release_button(button_pin)
    device.step_time(350)
    events = [
        decode_event(command.data)
        for command in wait_for_button_commands(device, 3)
    ]
    assert events == [
        (0, 1, BUTTON_DOWN, 0, 0),
        (1, 1, BUTTON_UP, 0, 0),
        (2, 1, BUTTON_N_CLICK, 1, 0),
    ]
    assert device.read_zigbee_attr(
        1, ZCL_CLUSTER_BUTTON_EVENT, ZCL_ATTR_BUTTON_EVENT_STATE
    ) == "0"
    assert device.read_zigbee_attr(
        1, ZCL_CLUSTER_BUTTON_EVENT, ZCL_ATTR_BUTTON_EVENT_LAST_EVENT_SEQ
    ) == "2"


def test_hold_preserves_press_id_and_duration(device, button_pin):
    device.clear_events()
    device.press_button(button_pin)
    device.step_time(800)
    device.release_button(button_pin)

    events = [
        decode_event(command.data)
        for command in wait_for_button_commands(device, 4)
    ]
    assert [event[2] for event in events] == [
        BUTTON_DOWN,
        BUTTON_HOLD_START,
        BUTTON_UP,
        BUTTON_HOLD_END,
    ]
    assert {event[1] for event in events} == {1}
    assert events[-1][4] >= 800


def test_timing_attributes_update_input_pipeline(device, button_pin):
    assert device.read_zigbee_attr(
        1, ZCL_CLUSTER_BUTTON_EVENT, ZCL_ATTR_BUTTON_EVENT_MULTI_CLICK_GAP
    ) == "350"
    assert device.read_zigbee_attr(
        1, ZCL_CLUSTER_BUTTON_EVENT, ZCL_ATTR_BUTTON_EVENT_DEBOUNCE_MS
    ) == "8"
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_BUTTON_EVENT, ZCL_ATTR_BUTTON_EVENT_MULTI_CLICK_GAP, 150
    )
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_BUTTON_EVENT, ZCL_ATTR_BUTTON_EVENT_DEBOUNCE_MS, 20
    )

    device.clear_events()
    device.set_gpio(button_pin, 0)
    device.step_time(10)
    assert button_commands(device) == []
    device.step_time(10)
    assert decode_event(wait_for_button_commands(device, 1)[0].data)[2] == BUTTON_DOWN
    device.set_gpio(button_pin, 1)
    device.step_time(20)
    device.step_time(150)
    assert decode_event(wait_for_button_commands(device, 3)[-1].data)[2:] == (
        BUTTON_N_CLICK,
        1,
        0,
    )


def test_not_joined_events_are_not_replayed(device, button_pin):
    device.set_network(0)
    device.clear_events()
    device.click_button(button_pin)
    device.step_time(350)
    assert button_commands(device) == []

    device.set_network(1)
    assert button_commands(device) == []
    device.click_button(button_pin)
    assert decode_event(wait_for_button_commands(device, 1)[0].data)[0] == 3


def test_basic_diagnostics_match_pipeline_counters(device, button_pin):
    device.click_button(button_pin)
    device.step_time(350)
    counters = device.counters()
    device.step_time(0)

    assert int(
        device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_GPIO_EDGES_CAPTURED
        )
    ) == counters["gpio_edges_captured"]
    assert int(
        device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_BUTTON_EVENTS_EMITTED
        )
    ) == counters["button_events_emitted"]
    assert int(
        device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_GESTURES_EMITTED
        )
    ) == counters["gestures_emitted"]
