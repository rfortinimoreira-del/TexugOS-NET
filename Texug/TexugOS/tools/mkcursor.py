import binascii
import struct
import sys
import zlib
from pathlib import Path


OUTPUT_WIDTH = 12
OUTPUT_HEIGHT = 16
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def paeth_predictor(left, above, upper_left):
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def read_rgba_png(path):
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path} is not a PNG image")

    offset = len(PNG_SIGNATURE)
    compressed = bytearray()
    width = height = bit_depth = color_type = interlace = None
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4 : offset + 8]
        start = offset + 8
        end = start + length
        chunk = data[start:end]
        expected_crc = struct.unpack_from(">I", data, end)[0]
        if binascii.crc32(kind + chunk) & 0xFFFFFFFF != expected_crc:
            raise ValueError(f"{path} contains a PNG chunk with an invalid CRC")
        offset = end + 4

        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", chunk)
            )
            if compression != 0 or filtering != 0:
                raise ValueError("unsupported PNG compression or filter method")
        elif kind == b"IDAT":
            compressed.extend(chunk)
        elif kind == b"IEND":
            break

    if (
        width is None
        or height is None
        or bit_depth != 8
        or color_type != 6
        or interlace != 0
    ):
        raise ValueError("cursor must be a non-interlaced 8-bit RGBA PNG")

    stride = width * 4
    decoded = zlib.decompress(compressed)
    if len(decoded) != height * (stride + 1):
        raise ValueError("PNG pixel data has an unexpected size")

    rows = []
    previous = bytearray(stride)
    for y in range(height):
        row_offset = y * (stride + 1)
        filter_type = decoded[row_offset]
        row = bytearray(decoded[row_offset + 1 : row_offset + 1 + stride])
        for index in range(stride):
            left = row[index - 4] if index >= 4 else 0
            above = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if filter_type == 1:
                row[index] = (row[index] + left) & 0xFF
            elif filter_type == 2:
                row[index] = (row[index] + above) & 0xFF
            elif filter_type == 3:
                row[index] = (row[index] + (left + above) // 2) & 0xFF
            elif filter_type == 4:
                predictor = paeth_predictor(left, above, upper_left)
                row[index] = (row[index] + predictor) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG row filter: {filter_type}")
        rows.append(row)
        previous = row
    return width, height, rows


def scale_cursor(width, height, rows):
    left = width
    top = height
    right = 0
    bottom = 0
    for y, row in enumerate(rows):
        for x in range(width):
            if row[x * 4 + 3] >= 128:
                left = min(left, x)
                top = min(top, y)
                right = max(right, x + 1)
                bottom = max(bottom, y + 1)
    if right <= left or bottom <= top:
        raise ValueError("cursor PNG has no visible pixels")

    crop_width = right - left
    crop_height = bottom - top
    pixels = []
    for output_y in range(OUTPUT_HEIGHT):
        y_start = top + output_y * crop_height // OUTPUT_HEIGHT
        y_end = top + (output_y + 1) * crop_height // OUTPUT_HEIGHT
        for output_x in range(OUTPUT_WIDTH):
            x_start = left + output_x * crop_width // OUTPUT_WIDTH
            x_end = left + (output_x + 1) * crop_width // OUTPUT_WIDTH
            alpha_sum = 0
            luminance_sum = 0
            sample_count = (x_end - x_start) * (y_end - y_start)
            for y in range(y_start, y_end):
                row = rows[y]
                for x in range(x_start, x_end):
                    pixel = x * 4
                    red, green, blue, alpha = row[pixel : pixel + 4]
                    alpha_sum += alpha
                    luminance_sum += (
                        (299 * red + 587 * green + 114 * blue) * alpha
                    )

            average_alpha = (alpha_sum + sample_count // 2) // sample_count
            if average_alpha < 32 or alpha_sum == 0:
                pixels.append(0xFF)
                continue
            luminance = luminance_sum // (1000 * alpha_sum)
            if average_alpha < 224:
                pixels.append(8 if luminance < 128 else 7)
            else:
                pixels.append(0 if luminance < 128 else 15)
    return pixels


def write_header(path, pixels):
    lines = [
        "#ifndef TEXUGOS_MOUSE_CURSOR_H",
        "#define TEXUGOS_MOUSE_CURSOR_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define MOUSE_CURSOR_WIDTH {OUTPUT_WIDTH}u",
        f"#define MOUSE_CURSOR_HEIGHT {OUTPUT_HEIGHT}u",
        "static const uint8_t mouse_cursor_pixels[MOUSE_CURSOR_WIDTH *",
        "                                                MOUSE_CURSOR_HEIGHT] = {",
    ]
    for y in range(OUTPUT_HEIGHT):
        row = pixels[y * OUTPUT_WIDTH : (y + 1) * OUTPUT_WIDTH]
        values = ", ".join(f"0x{value:02x}u" for value in row)
        lines.append(f"    {values},")
    lines.extend(["};", "", "#endif", ""])
    path.write_text("\n".join(lines), encoding="ascii")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: mkcursor.py INPUT.png OUTPUT.h")
    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    width, height, rows = read_rgba_png(input_path)
    pixels = scale_cursor(width, height, rows)
    write_header(output_path, pixels)


if __name__ == "__main__":
    main()
