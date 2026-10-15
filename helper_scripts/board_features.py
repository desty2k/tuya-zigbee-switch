#!/usr/bin/env python3
"""Per-board firmware features from device_db.yaml.

A board's features are its explicit `features` list, or, when the field is
absent, the features its config tokens need. The token classification mirrors
src/device_config/config_parser.c: a token belongs to a feature when the code
it instantiates is compiled only with that feature (src/features.mk).

The `probe` feature builds a GPIO probe image on a Telink router. A probe
board may add the relay and switch features, so a config string written over
Zigbee can drive the pins already found while the probe covers the rest.

  board_features.py check            validate every board, exit 1 on errors
  board_features.py features BOARD   print BOARD's features, exit 1 if invalid
"""

import argparse
import json
import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
DB_FILE = ROOT / "device_db.yaml"
SCHEMA_FILE = ROOT / "device_db.schema.json"
FEATURES_MK = ROOT / "src/features.mk"


def known_features():
    schema = json.loads(SCHEMA_FILE.read_text())
    board = schema["patternProperties"]["^[A-Z][A-Z0-9_]*$"]
    return board["properties"]["features"]["items"]["enum"]


def build_features():
    match = re.search(r"^ALL_FEATURES := (.*)$", FEATURES_MK.read_text(), re.MULTILINE)
    return match.group(1).split() if match else []


def token_feature(token):
    """Return (known, feature) for one pinout token; feature None is core."""
    if token == "SLP" or token == "M":
        return True, None
    if token[:1] in ("D", "G", "i", "K"):
        return True, None
    if token.startswith("BT"):
        return True, "battery"
    if token[:1] in ("B", "L", "I"):
        return True, None
    if token[:1] == "S":
        return True, "switch"
    if token[:1] == "R":
        return True, "relay"
    if token[:1] in ("C", "X"):
        return True, "cover"
    if token[:1] == "E":
        return True, "meter"
    return False, None


def config_strings(board):
    alt = board.get("alt_config_str")
    alts = alt if isinstance(alt, list) else [alt]
    return [text for text in [board.get("config_str"), *alts] if text]


def config_tokens(text):
    # The first two fields are the manufacturer and model names.
    return [token for token in text.split(";")[2:] if token]


def derived_features(board):
    features = set()
    for text in config_strings(board):
        for token in config_tokens(text):
            _, feature = token_feature(token)
            if feature is not None:
                features.add(feature)
    return features


def board_features(board, order):
    explicit = board.get("features")
    features = set(explicit) if explicit is not None else derived_features(board)
    return [feature for feature in order if feature in features]


# Features a probe image may carry next to `probe`: enough to test discovered
# buttons and relays from a config string written over Zigbee. Motor relays of
# a cover device are tested as plain relays in a `K` interlock group.
PROBE_COMPANION_FEATURES = {"relay", "switch"}


def check_probe_board(name, board, features):
    errors = []
    extra = features - {"probe"} - PROBE_COMPANION_FEATURES
    if extra:
        errors.append(f"{name}: feature 'probe' allows only "
                      f"{sorted(PROBE_COMPANION_FEATURES)} next to it, not {sorted(extra)}")
    if board.get("mcu_family") != "Telink":
        errors.append(f"{name}: feature 'probe' has a Telink HAL only")
    if board.get("device_type") != "router":
        errors.append(f"{name}: a probe board must be a router")
    return errors


def check_board(name, board, order):
    """Return (errors, warnings) for one board."""
    errors = []
    warnings = []
    explicit = board.get("features")
    if explicit is not None:
        for feature in explicit:
            if feature not in order:
                errors.append(f"{name}: unknown feature '{feature}'")
    features = set(board_features(board, order))
    buildable = board.get("build") in (True, "yes")
    if "probe" in features:
        errors += check_probe_board(name, board, features)
    for text in config_strings(board):
        for token in config_tokens(text):
            known, feature = token_feature(token)
            if not known:
                message = f"{name}: unknown config token '{token}'"
                (errors if buildable else warnings).append(message)
            elif feature is not None and feature not in features:
                errors.append(f"{name}: token '{token}' needs feature '{feature}', "
                              f"not in features {sorted(features)}")
    return errors, warnings


def load_db(path):
    with open(path) as db_file:
        return yaml.safe_load(db_file)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--db", default=str(DB_FILE))
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check")
    features_parser = sub.add_parser("features")
    features_parser.add_argument("board")
    args = parser.parse_args()

    db = load_db(args.db)
    order = known_features()

    if args.command == "features":
        if args.board not in db:
            sys.exit(f"{args.board}: not in {args.db}")
        errors, _ = check_board(args.board, db[args.board], order)
        if errors:
            sys.exit("\n".join(errors))
        print(" ".join(board_features(db[args.board], order)))
        return

    all_errors = []
    all_warnings = []
    if build_features() != order:
        all_errors.append(f"ALL_FEATURES in {FEATURES_MK.relative_to(ROOT)} "
                          f"{build_features()} differs from the schema enum {order}")
    for name, board in sorted(db.items()):
        errors, warnings = check_board(name, board, order)
        all_errors += errors
        all_warnings += warnings
    for warning in all_warnings:
        print(f"warning: {warning}", file=sys.stderr)
    for error in all_errors:
        print(f"error: {error}", file=sys.stderr)
    explicit = sum(1 for board in db.values() if board.get("features") is not None)
    print(f"{len(db)} boards checked ({explicit} explicit, {len(db) - explicit} derived): "
          f"{len(all_errors)} errors, {len(all_warnings)} warnings")
    if all_errors:
        sys.exit(1)


if __name__ == "__main__":
    main()
