#!/usr/bin/env python3
import argparse
import pathlib
import struct
import sys

FIXED_HEADER_SIZE = 24
NAME_MAX = 31
ICON_SIZE = 8
BINARY_MAX_SIZE = 4096
DEFAULT_ICON = bytes.fromhex("3c7e5a5a7e3c1818")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Package a flat 32-bit x86 binary as a Texug Executable."
    )
    parser.add_argument("--name", required=True)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--icon", default=DEFAULT_ICON.hex(),
                        help="8 rows encoded as 16 hexadecimal digits")
    parser.add_argument("--entry", type=lambda value: int(value, 0), default=0)
    args = parser.parse_args()

    try:
        name = args.name.encode("ascii")
    except UnicodeEncodeError as error:
        raise SystemExit("application name must be printable ASCII") from error
    if not name or len(name) > NAME_MAX or any(byte < 0x20 or byte > 0x7e
                                                for byte in name):
        raise SystemExit(f"application name must contain 1-{NAME_MAX} printable ASCII bytes")
    try:
        icon = bytes.fromhex(args.icon)
    except ValueError as error:
        raise SystemExit("icon must contain exactly 16 hexadecimal digits") from error
    if len(icon) != ICON_SIZE:
        raise SystemExit("icon must contain exactly 16 hexadecimal digits")

    binary = args.binary.read_bytes()
    if not binary or len(binary) > BINARY_MAX_SIZE:
        raise SystemExit(f"binary must contain 1-{BINARY_MAX_SIZE} bytes")
    if args.entry < 0 or args.entry >= len(binary):
        raise SystemExit("entry offset must point inside the binary")

    header_size = FIXED_HEADER_SIZE + len(name) + ICON_SIZE
    header = struct.pack(
        "<4sHHHBBIII",
        b"TEF\0",
        1,
        header_size,
        len(name),
        8,
        8,
        len(binary),
        args.entry,
        0,
    )
    args.output.write_bytes(header + name + icon + binary)


if __name__ == "__main__":
    main()
