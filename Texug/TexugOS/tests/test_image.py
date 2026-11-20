import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

IMAGE_PATH = pathlib.Path(os.environ.get("TEXUGOS_IMAGE", "build/texugos.img"))
SECTOR_SIZE = 512
BOOT_PAYLOAD_SECTORS = 512
BOOT_READ_CHUNK_SECTORS = 64


class BootImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = IMAGE_PATH.read_bytes()

    def test_raw_disk_image_has_expected_size_and_signature(self):
        self.assertEqual(len(self.image), 131072 * SECTOR_SIZE)
        self.assertEqual(self.image[510:512], b"\x55\xaa")

    def test_image_contains_fat32_lba_partition(self):
        partition_type = self.image[450]
        start_sector, sector_count = struct.unpack_from("<II", self.image, 454)
        self.assertEqual(partition_type, 0x0C)
        self.assertEqual(start_sector, 2048)
        self.assertEqual(sector_count, 131072 - start_sector)

        volume = start_sector * SECTOR_SIZE
        self.assertEqual(self.image[volume + 82 : volume + 90], b"FAT32   ")
        self.assertEqual(self.image[volume + 510 : volume + 512], b"\x55\xaa")

        reserved_sectors = struct.unpack_from("<H", self.image, volume + 14)[0]
        fat_count = self.image[volume + 16]
        fat_size = struct.unpack_from("<I", self.image, volume + 36)[0]
        root_cluster = struct.unpack_from("<I", self.image, volume + 44)[0]
        first_fat = volume + reserved_sectors * SECTOR_SIZE
        self.assertEqual(struct.unpack_from("<I", self.image, first_fat + 8)[0], 0x0FFFFFFF)
        self.assertEqual(fat_count, 2)
        self.assertEqual(root_cluster, 2)
        self.assertGreater(fat_size, 0)

    def test_boot_sector_loads_fixed_payload_from_lba_one(self):
        packet = self.image[:SECTOR_SIZE]
        self.assertIn(b"TexugOS: BIOS boot", packet)
        self.assertIn(b"TexugOS: payload read failed", packet)
        self.assertIn(struct.pack("<H", BOOT_READ_CHUNK_SECTORS), packet)
        self.assertIn(struct.pack("<Q", 1), packet)

    def test_payload_contains_boot_handoff_and_e820_messages(self):
        payload = self.image[
            SECTOR_SIZE : (BOOT_PAYLOAD_SECTORS + 1) * SECTOR_SIZE
        ]
        self.assertIn(b"TexugOS: collecting E820 memory map", payload)
        self.assertIn(b"TexugOS: E820 memory map failed", payload)
        self.assertIn(b"Kernel subsystems active (partial).", payload)
        self.assertIn(struct.pack("<I", 0x54455847), payload)

    def test_optional_rtl8192se_firmware_is_added_to_fat32(self):
        firmware = bytes(index % 251 for index in range(1200))
        with tempfile.TemporaryDirectory(prefix="texugos-firmware-image-") as directory:
            temp = pathlib.Path(directory)
            boot_path = temp / "boot.bin"
            payload_path = temp / "payload.bin"
            firmware_path = temp / "rtl8192sefw.bin"
            output_path = temp / "image.img"
            boot_path.write_bytes(self.image[:SECTOR_SIZE])
            payload_path.write_bytes(
                self.image[
                    SECTOR_SIZE : (BOOT_PAYLOAD_SECTORS + 1) * SECTOR_SIZE
                ]
            )
            firmware_path.write_bytes(firmware)
            subprocess.run(
                [
                    sys.executable,
                    str(pathlib.Path(__file__).parents[1] / "tools" / "mkimage.py"),
                    str(boot_path),
                    str(payload_path),
                    str(output_path),
                    "--rtl8192se-firmware",
                    str(firmware_path),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

            image = output_path.read_bytes()
        volume_start = 2048 * SECTOR_SIZE
        reserved_sectors = struct.unpack_from("<H", image, volume_start + 14)[0]
        fat_count = image[volume_start + 16]
        fat_sectors = struct.unpack_from("<I", image, volume_start + 36)[0]
        cluster_count = (
            131072 - 2048 - reserved_sectors - fat_count * fat_sectors
        )
        data_start = 2048 + reserved_sectors + fat_count * fat_sectors
        root_offset = data_start * SECTOR_SIZE
        entry = image[root_offset : root_offset + 32]
        self.assertEqual(entry[:11], b"RTL8192 BIN")
        self.assertEqual(struct.unpack_from("<I", entry, 28)[0], len(firmware))

        cluster = struct.unpack_from("<H", entry, 26)[0]
        first_fat = (2048 + reserved_sectors) * SECTOR_SIZE
        stored = bytearray()
        seen = set()
        while cluster < 0x0FFFFFF8:
            self.assertGreaterEqual(cluster, 3)
            self.assertLess(cluster, cluster_count + 2)
            self.assertNotIn(cluster, seen)
            seen.add(cluster)
            data_sector = data_start + cluster - 2
            data_offset = data_sector * SECTOR_SIZE
            stored.extend(image[data_offset : data_offset + SECTOR_SIZE])
            cluster = struct.unpack_from("<I", image, first_fat + cluster * 4)[0]
        self.assertEqual(bytes(stored[: len(firmware)]), firmware)
        fs_info_offset = volume_start + SECTOR_SIZE
        free_clusters = struct.unpack_from("<I", image, fs_info_offset + 488)[0]
        self.assertEqual(free_clusters, cluster_count - 1 - len(seen))


if __name__ == "__main__":
    unittest.main()
