#!/usr/bin/env python3
import argparse
import pathlib


MAX_FIRMWARE_SIZE = 90000


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--input", type=pathlib.Path)
    args = parser.parse_args()

    data = args.input.read_bytes() if args.input is not None else b""
    if len(data) > MAX_FIRMWARE_SIZE:
        raise SystemExit(
            f"firmware must not exceed {MAX_FIRMWARE_SIZE} bytes"
        )

    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset : offset + 12]
        lines.append(", ".join(f"0x{byte:02x}u" for byte in chunk))
    values = ",\n    ".join(lines) if lines else "0u"
    contents = (
        "#ifndef TEXUGOS_RTL8192SE_FIRMWARE_BLOB_H\n"
        "#define TEXUGOS_RTL8192SE_FIRMWARE_BLOB_H\n\n"
        "#include <stdint.h>\n\n"
        f"#define RTL8192SE_EMBEDDED_FIRMWARE_SIZE {len(data)}u\n"
        "static const uint8_t rtl8192se_embedded_firmware[] = {\n"
        f"    {values}\n"
        "};\n\n"
        "#endif\n"
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(contents, encoding="ascii")


if __name__ == "__main__":
    main()
