#!/usr/bin/env python3
import pathlib
import struct
import sys

SECTOR_SIZE = 512
BOOT_SECTORS = 512
IMAGE_SECTORS = 131072
PARTITION_START = 2048
PARTITION_TYPE_FAT32_LBA = 0x0C
FAT_RESERVED_SECTORS = 32
FAT_COUNT = 2
FAT_ROOT_CLUSTER = 2


def main() -> None:
    if len(sys.argv) < 4:
        raise SystemExit(
            "usage: mkimage.py BOOT_SECTOR PAYLOAD OUTPUT "
            "[--rtl8192se-firmware FILE] [--tef-file FILE]"
        )

    boot_path, payload_path, output_path = map(pathlib.Path, sys.argv[1:4])
    boot = boot_path.read_bytes()
    payload = payload_path.read_bytes()
    firmware_path = None
    tef_path = None
    options = sys.argv[4:]
    option_index = 0
    while option_index < len(options):
        if option_index + 1 >= len(options):
            raise SystemExit("image option requires a file path")
        option = options[option_index]
        path = pathlib.Path(options[option_index + 1])
        if option == "--rtl8192se-firmware" and firmware_path is None:
            firmware_path = path
        elif option == "--tef-file" and tef_path is None:
            tef_path = path
        else:
            raise SystemExit(f"unknown or duplicate image option: {option}")
        option_index += 2
    firmware = firmware_path.read_bytes() if firmware_path is not None else b""
    tef_file = tef_path.read_bytes() if tef_path is not None else b""

    if len(boot) != SECTOR_SIZE or boot[510:512] != b"\x55\xaa":
        raise SystemExit("boot sector must be exactly 512 bytes and end in 0x55aa")
    if not payload or len(payload) > BOOT_SECTORS * SECTOR_SIZE:
        raise SystemExit(
            f"payload must fit in {BOOT_SECTORS * SECTOR_SIZE} bytes"
        )
    if len(firmware) > 90000:
        raise SystemExit("RTL8192SE firmware must not exceed 90000 bytes")
    if firmware_path is not None and not firmware:
        raise SystemExit("RTL8192SE firmware file must not be empty")
    if tef_path is not None and not tef_file:
        raise SystemExit("TEF executable file must not be empty")

    partition_sectors = IMAGE_SECTORS - PARTITION_START
    firmware_cluster_count = (
        (len(firmware) + SECTOR_SIZE - 1) // SECTOR_SIZE
    )
    tef_cluster_count = (len(tef_file) + SECTOR_SIZE - 1) // SECTOR_SIZE
    allocated_file_clusters = firmware_cluster_count + tef_cluster_count
    fat_sectors = 1
    while True:
        cluster_count = (
            partition_sectors - FAT_RESERVED_SECTORS - FAT_COUNT * fat_sectors
        )
        required_fat_sectors = (
            (cluster_count + 2) * 4 + SECTOR_SIZE - 1
        ) // SECTOR_SIZE
        if required_fat_sectors == fat_sectors:
            break
        fat_sectors = required_fat_sectors

    image = bytearray(IMAGE_SECTORS * SECTOR_SIZE)
    image[:SECTOR_SIZE] = boot
    start = SECTOR_SIZE
    image[start : start + len(payload)] = payload

    partition_entry = (
        b"\x00\xfe\xff\xff"
        + bytes((PARTITION_TYPE_FAT32_LBA,))
        + b"\xfe\xff\xff"
        + struct.pack("<II", PARTITION_START, partition_sectors)
    )
    image[446:462] = partition_entry

    boot_sector = bytearray(SECTOR_SIZE)
    boot_sector[0:3] = b"\xeb\x58\x90"
    boot_sector[3:11] = b"TEXUGOS "
    struct.pack_into("<H", boot_sector, 11, SECTOR_SIZE)
    boot_sector[13] = 1
    struct.pack_into("<H", boot_sector, 14, FAT_RESERVED_SECTORS)
    boot_sector[16] = FAT_COUNT
    boot_sector[21] = 0xF8
    struct.pack_into("<H", boot_sector, 24, 63)
    struct.pack_into("<H", boot_sector, 26, 255)
    struct.pack_into("<I", boot_sector, 28, PARTITION_START)
    struct.pack_into("<I", boot_sector, 32, partition_sectors)
    struct.pack_into("<I", boot_sector, 36, fat_sectors)
    struct.pack_into("<I", boot_sector, 44, FAT_ROOT_CLUSTER)
    struct.pack_into("<H", boot_sector, 48, 1)
    struct.pack_into("<H", boot_sector, 50, 6)
    boot_sector[64] = 0x80
    boot_sector[66] = 0x29
    struct.pack_into("<I", boot_sector, 67, 0x54455847)
    boot_sector[71:82] = b"TEXUGOS    "
    boot_sector[82:90] = b"FAT32   "
    boot_sector[510:512] = b"\x55\xaa"
    volume_start = PARTITION_START * SECTOR_SIZE
    image[volume_start : volume_start + SECTOR_SIZE] = boot_sector
    image[volume_start + 6 * SECTOR_SIZE : volume_start + 7 * SECTOR_SIZE] = (
        boot_sector
    )

    fs_info = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fs_info, 0, 0x41615252)
    struct.pack_into("<I", fs_info, 484, 0x61417272)
    struct.pack_into(
        "<I", fs_info, 488, cluster_count - 1 - allocated_file_clusters
    )
    struct.pack_into("<I", fs_info, 492, 3 + allocated_file_clusters)
    struct.pack_into("<I", fs_info, 508, 0xAA550000)
    image[volume_start + SECTOR_SIZE : volume_start + 2 * SECTOR_SIZE] = fs_info
    image[volume_start + 7 * SECTOR_SIZE : volume_start + 8 * SECTOR_SIZE] = fs_info

    fat = bytearray(fat_sectors * SECTOR_SIZE)
    struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF)
    fat_start = PARTITION_START + FAT_RESERVED_SECTORS
    fat_bytes = fat_sectors * SECTOR_SIZE
    if allocated_file_clusters + FAT_ROOT_CLUSTER >= cluster_count + 2:
        raise SystemExit("embedded files do not fit in the FAT32 volume")

    data_start = PARTITION_START + FAT_RESERVED_SECTORS + FAT_COUNT * fat_sectors
    root_offset = data_start * SECTOR_SIZE
    first_cluster = 3
    root_slot = 0
    embedded_files = []
    if firmware:
        embedded_files.append((b"RTL8192 BIN", firmware))
    if tef_file:
        embedded_files.append((b"HELLO   TEF", tef_file))

    for short_name, contents in embedded_files:
        file_cluster_count = (
            len(contents) + SECTOR_SIZE - 1
        ) // SECTOR_SIZE
        entry = bytearray(32)
        entry[0:11] = short_name
        entry[11] = 0x20
        struct.pack_into("<H", entry, 26, first_cluster)
        struct.pack_into("<I", entry, 28, len(contents))
        entry_offset = root_offset + root_slot * len(entry)
        image[entry_offset : entry_offset + len(entry)] = entry
        root_slot += 1

        for index in range(file_cluster_count):
            cluster = first_cluster + index
            next_cluster = (
                cluster + 1
                if index + 1 < file_cluster_count
                else 0x0FFFFFFF
            )
            struct.pack_into("<I", fat, cluster * 4, next_cluster)
            source_start = index * SECTOR_SIZE
            chunk = contents[source_start : source_start + SECTOR_SIZE]
            data_sector = data_start + cluster - FAT_ROOT_CLUSTER
            data_offset = data_sector * SECTOR_SIZE
            image[data_offset : data_offset + len(chunk)] = chunk
        first_cluster += file_cluster_count

    fat_offset = fat_start * SECTOR_SIZE
    image[fat_offset : fat_offset + fat_bytes] = fat
    second_fat_offset = (fat_start + fat_sectors) * SECTOR_SIZE
    image[second_fat_offset : second_fat_offset + fat_bytes] = fat

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(image)


if __name__ == "__main__":
    main()
