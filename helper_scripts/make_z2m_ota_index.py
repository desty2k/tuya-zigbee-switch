import argparse
import hashlib
import json
from pathlib import Path
import yaml
import subprocess


BOARD_TO_MANUFACTURER_NAMES = {
    "TS0011": [
        '_TZ3000_qmi1cfuq',
        '_TZ3000_txpirhfq',
        '_TZ3000_ji4araar',
        'Tuya-TS0011-custom',
    ],
    "TS0011_END_DEVICE": [
        '_TZ3000_qmi1cfuq',
        '_TZ3000_txpirhfq',
        '_TZ3000_ji4araar',
        'Tuya-TS0011-custom',
    ],
    "TS0012": [
        '_TZ3000_jl7qyupf',
        '_TZ3000_nPGIPl5D',
        '_TZ3000_kpatq5pq',
        '_TZ3000_ljhbw1c9',
        '_TZ3000_4zf0crgo',
        'Tuya-CUSTOM',
        'Tuya-TS0012-custom',
    ],
    "TS0012_END_DEVICE": [
        '_TZ3000_jl7qyupf',
        '_TZ3000_nPGIPl5D',
        '_TZ3000_kpatq5pq',
        '_TZ3000_ljhbw1c9',
        '_TZ3000_4zf0crgo',
        'Tuya-CUSTOM',
        'Tuya-TS0012-custom',
    ],
    "TS0001": [
        '_TZ3000_skueekg3',
        'Tuya-TS0001-custom',
    ],
    "TS0002": [
        '_TZ3000_01gpyda5',
        '_TZ3000_bvrlqyj7',
        '_TZ3000_7ed9cqgi',
        '_TZ3000_zmy4lslw',
        '_TZ3000_ruxexjfz',
        '_TZ3000_4xfqlgqo',
        '_TZ3000_hojntt34',
        '_TZ3000_eei0ubpy',
        '_TZ3000_qaa59zqd',
        '_TZ3000_lmlsduws',
        '_TZ3000_lugaswf8',
        '_TZ3000_fbjdkph9',
        'Tuya-TS0002-custom',
    ],
    
}


def make_ota_index_entry(file: Path, base_url: str, manufacturer_names: list[str] | None) -> dict[str, str | int]:
    data = file.read_bytes()
    res = {
        "fileName": file.name,
        "fileVersion": int.from_bytes(data[14:18], "little"),
        "fileSize": len(data),
        
        "url": f"{base_url}/{file}",
        "imageType": int.from_bytes(data[12:14], "little"),
        "manufacturerCode": int.from_bytes(data[10:12], "little"),
        "sha512": hashlib.sha512(data).hexdigest(),
        "otaHeaderString": data[20:52].decode('unicode_escape'), 
    }
    if manufacturer_names:
        res["manufacturerName"] = manufacturer_names
    return res

def board_manufacturer_names(db: dict, board: str | None) -> list[str]:
    """manufacturerName filter of a board's entries, without duplicates."""
    device = db.get(board) if board else None
    if not device:
        return []
    names = []
    if device.get("stock_manufacturer_name"):
        names.append(device["stock_manufacturer_name"])
    if device.get("old_manufacturer_names"):
        names.extend(device["old_manufacturer_names"])
    names.append(device["config_str"].split(";")[0])
    return list(dict.fromkeys(names))


def stock_image_types(db: dict) -> set[int]:
    return {device["stock_image_type"] for device in db.values()
            if isinstance(device, dict) and device.get("stock_image_type") is not None}


def check_entry(entry: dict, db: dict) -> None:
    """Refuse an entry that a stock device of another model could accept.

    Stock Tuya images share image types (54179 is used by many models), so an
    entry with a stock image type must be restricted by manufacturerName.
    """
    if entry["imageType"] in stock_image_types(db) and not entry.get("manufacturerName"):
        raise SystemExit(
            f"{entry['fileName']}: image type {entry['imageType']} is a stock image type; "
            "refusing an index entry without a manufacturerName filter")


REPO_INDEX_DIR = (Path(__file__).resolve().parent.parent / "zigbee2mqtt" / "ota").resolve()


def probe_boards(db: dict) -> set[str]:
    return {name for name, device in db.items()
            if isinstance(device, dict) and "probe" in (device.get("features") or [])}


def check_publishable(index_file: Path, board: str | None, index_data: list[dict], db: dict) -> None:
    """Keep GPIO probe images out of the repository indexes.

    Zigbee2MQTT installs read these indexes directly, and a probe's from_tuya
    entry matches every stock device with the probe board's manufacturer name.
    A probe image is installed from a local index outside zigbee2mqtt/ota/.
    """
    if index_file.resolve().parent != REPO_INDEX_DIR:
        return
    probes = probe_boards(db)
    if board in probes:
        raise SystemExit(
            f"{board} is a GPIO probe board; refusing to add it to {index_file}. "
            "Write a local index outside zigbee2mqtt/ota/ instead (docs/contribute/porting.md)")
    for entry in index_data:
        if any(f"/{probe}/" in entry.get("url", "") for probe in probes):
            raise SystemExit(f"{index_file}: entry {entry['url']} is a GPIO probe image")


# Index URLs always name the fork: its images are not published anywhere else.
FORK_URL = "https://github.com/desty2k/tuya-zigbee-switch"


def get_raw_github_link():
    try:
        branch = subprocess.run(
            ["git", "branch", "--show-current"],
            capture_output=True, text=True, check=True
        ).stdout.strip() or "main"
    except subprocess.CalledProcessError:
        branch = "main"
    return f"{FORK_URL}/raw/refs/heads/{branch}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create Zigbee2mqtt index json",
        epilog="Reads a zigbee image file and updates index.json")
    parser.add_argument("filename", metavar="INPUT", type=str, help="OTA filename")
    parser.add_argument("index_file", type=str, help="OTA index.json file")
    parser.add_argument("--base-url", required=False, help="Base url to use", 
                        default=get_raw_github_link())
    parser.add_argument(
        "--db_file", metavar="INPUT", type=str, help="File with device db"
    )
    parser.add_argument("--board", required=False, help="Used to select manufacturerName list to avoid flashing wrong devices")
  

    args = parser.parse_args()

    db_str = Path(args.db_file).read_text()
    db = yaml.safe_load(db_str)

    manufacturer_names = board_manufacturer_names(db, args.board)

    entry = make_ota_index_entry(
        file=Path(args.filename),
        base_url=args.base_url,
        manufacturer_names=manufacturer_names,
    )
    index_file = Path(args.index_file)
    if index_file.exists():
        index_data = json.loads(index_file.read_text())
    else:
        index_data = []
    index_data = [
        it for it in index_data if
        (
            it.get("manufacturerName") != entry.get("manufacturerName")
            or it["manufacturerCode"] != entry["manufacturerCode"]    
            or it["imageType"] != entry["imageType"]
        ) 
    ]
    index_data.append(entry)
    for indexed in index_data:
        check_entry(indexed, db)
    check_publishable(index_file, args.board, index_data, db)
    index_file.write_text(json.dumps(
        index_data,
        indent=2,
    ))
    exit(0)