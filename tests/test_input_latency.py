from conftest import Device


def test_delayed_worker_preserves_every_valid_transition(
    device: Device, button_pin: str
) -> None:
    device.clear_events()
    device.inject_edges(button_pin, [(0, 0), (1, 20), (0, 40), (1, 60)])
    device.time_advance(40)
    device.tasks_poll()

    events = [event for event in device._events if event.kind == "btn_event"]
    assert [event.payload["type"] for event in events] == [
        "DOWN",
        "UP",
        "DOWN",
        "UP",
    ]
    assert [int(event.payload["t"]) for event in events] == [0, 20, 40, 60]
    assert [int(event.payload["press_id"]) for event in events] == [1, 1, 2, 2]
