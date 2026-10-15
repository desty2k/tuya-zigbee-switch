"""OTA index safety and probe board rules of the helper scripts."""

import json
import struct
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "helper_scripts"))

import board_features  # noqa: E402

INDEX_SCRIPT = ROOT / "helper_scripts/make_z2m_ota_index.py"
STOCK_IMAGE_TYPE = 54179
PROBE_BOARD = "OUTLET_BSEED_TS011F_PROBE"


def ota_file(path: Path, image_type: int, manufacturer: int = 4417) -> Path:
    header = bytearray(56)
    header[0:4] = b"\x1e\xf1\xee\x0b"
    header[10:12] = manufacturer.to_bytes(2, "little")
    header[12:14] = image_type.to_bytes(2, "little")
    header[14:18] = (1).to_bytes(4, "little")
    header[20:52] = b"probe test".ljust(32, b"\x00")
    path.write_bytes(bytes(header) + b"\x00" * 64)
    return path


def run_index(tmp_path: Path, image_type: int, board: str | None,
              db: Path = ROOT / "device_db.yaml") -> subprocess.CompletedProcess:
    args = [sys.executable, str(INDEX_SCRIPT), "--db_file", str(db),
            "--base-url", "https://example.invalid",
            str(ota_file(tmp_path / "image.zigbee", image_type)),
            str(tmp_path / "index.json")]
    if board:
        args += ["--board", board]
    return subprocess.run(args, capture_output=True, text=True)


def test_index_refuses_stock_image_type_without_manufacturer_filter(tmp_path: Path) -> None:
    result = run_index(tmp_path, STOCK_IMAGE_TYPE, board=None)
    assert result.returncode != 0
    assert "manufacturerName" in result.stderr
    assert not (tmp_path / "index.json").exists()


def test_index_refuses_an_unfiltered_stock_entry_already_in_the_index(tmp_path: Path) -> None:
    (tmp_path / "index.json").write_text(json.dumps([{
        "fileName": "old.zigbee", "imageType": STOCK_IMAGE_TYPE,
        "manufacturerCode": 4417,
    }]))
    result = run_index(tmp_path, 47116, board=PROBE_BOARD)
    assert result.returncode != 0
    assert "old.zigbee" in result.stderr


def test_index_accepts_fork_image_type_without_board(tmp_path: Path) -> None:
    assert run_index(tmp_path, 47116, board=None).returncode == 0


def test_probe_stock_entry_is_filtered_to_its_outlet(tmp_path: Path) -> None:
    result = run_index(tmp_path, STOCK_IMAGE_TYPE, board=PROBE_BOARD)
    assert result.returncode == 0, result.stderr
    (entry,) = json.loads((tmp_path / "index.json").read_text())
    assert entry["imageType"] == STOCK_IMAGE_TYPE
    assert entry["manufacturerCode"] == 4417
    assert entry["manufacturerName"] == ["_TZ3210_drzlxjne"]


def test_probe_board_is_a_unique_telink_probe_image_with_relay_and_switch() -> None:
    db = yaml.safe_load((ROOT / "device_db.yaml").read_text())
    board = db[PROBE_BOARD]
    order = board_features.known_features()
    assert board_features.board_features(board, order) == ["relay", "switch", "probe", "interlock"]
    assert board_features.check_board(PROBE_BOARD, board, order) == ([], [])
    assert (board["stock_manufacturer_id"], board["stock_image_type"]) == (4417, 54179)
    assert board["build"] in (False, "no")
    image_types = [other["firmware_image_type"] for other in db.values()]
    assert image_types.count(board["firmware_image_type"]) == 1


@pytest.mark.parametrize("change, message", [
    ({"features": ["probe", "relay", "cover"]}, "allows only"),
    ({"features": ["probe", "meter"]}, "allows only"),
    ({"mcu_family": "Silabs"}, "Telink"),
    ({"device_type": "end_device"}, "router"),
    ({"features": ["probe"]}, "needs feature 'relay'"),
])
def test_probe_board_rules(change: dict, message: str) -> None:
    db = yaml.safe_load((ROOT / "device_db.yaml").read_text())
    board = {**db[PROBE_BOARD], **change}
    errors, _ = board_features.check_board(PROBE_BOARD, board, board_features.known_features())
    assert any(message in error for error in errors), errors


@pytest.mark.parametrize("change", [
    {"features": ["probe"], "config_str": "_TZ3210_drzlxjne;TS011F-probe;"},
    {"features": ["probe", "relay"], "config_str": "_TZ3210_drzlxjne;TS011F-probe;RA0;RA1;K03;"},
])
def test_probe_board_variants_are_accepted(change: dict) -> None:
    db = yaml.safe_load((ROOT / "device_db.yaml").read_text())
    board = {**db[PROBE_BOARD], **change}
    assert board_features.check_board(PROBE_BOARD, board, board_features.known_features()) == ([], [])


@pytest.mark.parametrize("board, interlock", [
    ({"config_str": "m;t;SA0u;RA1;"}, False),
    ({"config_str": "m;t;SA0u;RA1;", "features": ["relay", "switch", "meter"]}, False),
    ({"config_str": "m;t;RA0;RA1;", "features": ["relay"]}, True),
    ({"config_str": "m;t;RA0;", "alt_config_str": ["m;t;RA0;RA1;"]}, True),
    ({"config_str": "m;t;RA0;K01;"}, True),
    ({"config_str": "m;t;CA0A1;"}, True),
])
def test_interlock_is_added_to_boards_that_can_use_it(board: dict, interlock: bool) -> None:
    order = board_features.known_features()
    assert ("interlock" in board_features.board_features(board, order)) == interlock
    assert board_features.check_board("BOARD", board, order)[0] == []


def test_discovery_helper_decodes_probe_attributes_and_proposes_a_config() -> None:
    import probe_discovery as pd

    raw_pins = struct.pack("<HHH", 0x0000, 0x0203, 0x0302)
    pins = pd.parse_pins(raw_pins)
    assert pins == ["A0", "C3", "D2"]
    assert pd.parse_classes(pins, bytes([1, 4, 2])) == {
        "A0": "FLOATING", "C3": "ACTIVE", "D2": "DRIVEN_HIGH"}
    assert pd.parse_levels(pins, 0b100) == {"A0": 0, "C3": 0, "D2": 1}
    elapsed, records = pd.parse_watch(
        struct.pack("<I", 2000) + struct.pack("<HIIIBB", 0x0203, 2000, 1000, 1000, 1, 0)
        + struct.pack("<HIIIBB", 0x0204, 2000, 40, 40, 0, 2))
    assert elapsed == 2000
    assert records["C3"].edges == 2000 and not records["C3"].over_rate
    assert records["C4"].over_rate
    assert pd.rate_hz(records["C3"], elapsed) == 1000.0

    found = pd.Findings(idle_hz={"C3": 1000.0, "C2": 0.0},
                        loaded_hz={"C3": 1000.0, "C2": 250.0},
                        button_edges={"D2": 3, "A1": 0}, relay=["A0"],
                        leds={"B5": 0}, sel=["B4"])
    config, meter = pd.propose(found, "drzlxjne", "TS011F-BS-PM")
    assert config == "drzlxjne;TS011F-BS-PM;LB5i;SD2u;RA0;M;"
    assert meter == {"ic": "BL0937", "cf": "C2", "cf1": "C3", "sel": "B4"}
    _, silent = pd.propose(pd.Findings(), "x", "y")
    assert "BL0942" in silent["ic"]


def test_index_refuses_a_probe_board_in_the_repository_indexes(tmp_path: Path) -> None:
    index = ROOT / "zigbee2mqtt/ota/index_probe_guard_test.json"
    try:
        result = subprocess.run(
            [sys.executable, str(INDEX_SCRIPT), "--db_file", str(ROOT / "device_db.yaml"),
             "--board", PROBE_BOARD,
             str(ota_file(tmp_path / "image.zigbee", STOCK_IMAGE_TYPE)), str(index)],
            capture_output=True, text=True)
        assert result.returncode != 0
        assert "GPIO probe board" in result.stderr
        assert not index.exists()
    finally:
        index.unlink(missing_ok=True)


def test_board_build_refuses_a_probe_board() -> None:
    result = subprocess.run(["make", "-s", "-f", "board.mk", "check-publishable",
                             f"BOARD={PROBE_BOARD}"],
                            cwd=ROOT, capture_output=True, text=True)
    assert result.returncode != 0
    assert "GPIO probe board" in result.stderr


def test_repository_indexes_hold_no_probe_entry() -> None:
    db = yaml.safe_load((ROOT / "device_db.yaml").read_text())
    probes = [name for name, device in db.items()
              if isinstance(device, dict) and "probe" in (device.get("features") or [])]
    assert probes
    for index in (ROOT / "zigbee2mqtt/ota").glob("index_*.json"):
        text = index.read_text()
        assert not any(f"/{probe}/" in text for probe in probes), index


def test_discovery_helper_reads_one_attribute_per_request() -> None:
    import probe_discovery as pd

    values = {"status": 0, "pins": "0000", "classes": "01", "levels": 1, "watch": "",
              "state": 0}
    ids = {attr_id: name.removeprefix("PROBE_").lower()
           for attr_id, name, *_ in pd.PROBE["attributes"]}
    requests = []

    class FakeClient:
        def publish(self, topic: str, body: str) -> None:
            attributes = json.loads(body)["read"]["attributes"]
            requests.append(attributes)
            reply = {"probe": {ids[attr]: values[ids[attr]] for attr in attributes}}
            probe._on_message(None, None, type("M", (), {"payload": json.dumps(reply)}))

    probe = object.__new__(pd.Z2MProbe)
    probe.topic, probe.state, probe.client = "z2m/probe", {}, FakeClient()
    probe.updated = pd.threading.Event()
    for name in ("status", "pins", "classes", "levels", "watch"):
        assert probe.read(name) == values[name]
    assert requests and all(len(attributes) == 1 for attributes in requests)


import firmware_version  # noqa: E402


@pytest.mark.parametrize(
    "text, build, expected",
    [
        ("2.0.0-rc.1", 0, 0x20000100),
        ("2.0.0-rc.1", 3, 0x20000103),
        ("2.0.0-rc.2", 0, 0x20000200),
        ("2.0.0", 0, 0x2000FF00),
        ("2.0.1-rc.1", 0, 0x20010100),
        ("2.1.0", 0, 0x2100FF00),
    ],
)
def test_file_version_encoding(text, build, expected):
    assert firmware_version.file_version(firmware_version.parse(text), build) == expected


def test_file_versions_order_releases_candidates_and_development_builds():
    order = [("1.1.2", 0), ("2.0.0-rc.1", 0), ("2.0.0-rc.1", 7), ("2.0.0-rc.2", 0), ("2.0.0", 0), ("2.0.0", 1), ("2.0.1-rc.1", 0)]
    values = [firmware_version.file_version(firmware_version.parse(t), b) for t, b in order]
    assert values == sorted(values) and len(set(values)) == len(values)
    # Every 2.x build is newer than the fork's 1.1.2 builds (0x1102_30xx).
    assert values[1] > 0x110230FF


@pytest.mark.parametrize("text", ["2.0", "2.0.0-rc", "2.0.0-rc.0", "2.0.0-rc.255", "10.0.0", "2.10.0", "2.0.256", "2.0.0-beta.1", "v2.0.0"])
def test_invalid_versions_are_rejected(text):
    with pytest.raises(ValueError):
        firmware_version.parse(text)


@pytest.mark.parametrize("build", [-1, 256])
def test_build_number_out_of_range_is_rejected(build):
    with pytest.raises(ValueError):
        firmware_version.file_version(firmware_version.parse("2.0.0"), build)


def test_version_string_fits_the_basic_sw_build_id():
    assert firmware_version.version_str("2.0.0-rc.1\n", "56fa5bb4") == "2.0.0-rc.1-56fa5"
    assert firmware_version.version_str("2.0.0", "") == "2.0.0"


def test_repository_version_file_is_valid():
    firmware_version.parse((ROOT / "VERSION").read_text())
